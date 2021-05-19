#!/usr/bin/perl
#
# Scan log rotation here and generate an m3u8
# in case your browser doesn't want to touch
# m2ts files, or just to make it all scannable.
mkdir("meta",0755);
opendir(X,".") || die;

my @files;
my $ext = undef;
my $conv = undef;
my $prefix = undef;
my $m3u8_name = "hls";

for ($i=0;$i < @ARGV;) {
    my $arg = $ARGV[$i++];

    if ($arg =~ s/^-+//) {
        if ($arg eq "ext") {
            $ext = $ARGV[$i++];
        }
        elsif ($arg eq "pre") {
            $prefix = $ARGV[$i++];
        }
        elsif ($arg eq "m3u8") {
            $m3u8_name = $ARGV[$i++];
        }
        elsif ($arg eq "to-mpegts") {
            $conv = "mpegts";
        }
        else {
            die "Unknown switch $arg";
        }
    }
    else {
        die "unexpected arg";
    }
}

if (defined($ext)) {
    @files = grep { $_ =~ m/\.$ext$/ } sort readdir(X);
}
else {
    @files = grep { $_ =~ m/\.(aac|m2ts|mp3|ts)$/ } sort readdir(X);
}

if (defined($prefix)) {
    @files = grep { $_ =~ m/^$prefix/ } @files;
}

my @durations;
my $maxdur = 0;
my $maxdiv = 0;
for ($i=0;$i < (@files-1);$i++) { # not the last fragment, because it's probably in progress
    my $file = $files[$i];

    print "\x0DScanning $file..."; $|++;

    my $duration = 0;

    if ( $i < (@files-10) && -f "meta/$file.duration" ) {
        open(A,"<meta/$file.duration") || die;
        $duration = <A>; chomp $duration;
        close(A);
    }
    if ($duration == 0) {
        my $line = `ffprobe -of ini -show_format $file 2>/dev/null | grep duration= `; chomp $line;
        my $name='',$value='';
        my $i = index($line,'=');
        if ($i > 0) {
            $name = substr($line,0,$i);
            $value = substr($line,$i+1);
        }
        if ($name eq "duration") {
            $duration = $value + 0.0;
        }
        if ($duration > 0) {
            open(A,">meta/$file.duration") || die;
            print A $duration;
            close(A);
        }
    }

    if ($duration > 0) {
        $maxdur += $duration;
        $maxdiv++;
    }
    push(@durations,$duration);
}
closedir(X);
$maxdur /= $maxdiv if $maxdiv > 0;
print "\x0DGenerating M3U8                                 "; $|++;

open(W,">$m3u8_name.m3u8.tmp") || die;
print W "#EXTM3U\n";
print W "#EXT-X-PLAYLIST-TYPE:EVENT\n";
print W "#EXT-X-VERSION:4\n";
print W "#EXT-X-MEDIA-SEQUENCE:0\n";
print W "#EXT-X-TARGETDURATION:".int($maxdur+0.5)."\n";
# skip the last one
for ($i=0;$i < (@files-1);$i++) {
    my $file = $files[$i];
    next if $file =~ m/[\?\#\%]/;
    my $duration = $durations[$i];
    next if $duration == 0;

    print W "#EXTINF:$duration,\n";

    if ($conv eq "mpegts") {
        print W "./aac2ts.cgi?$file\n"; # aac2ts.cgi must reside in the directory, no / allowed
    }
    else {
        print W "$file\n";
    }
}
close(W);

# for some stupid reason the Firefox HLS player won't provide any controls to go back or forward unless there's an #EXT-X-ENDLIST
# even though the same plugin in Chrome works properly :(
system("cp -f $m3u8_name.m3u8.tmp $m3u8_name.vod.m3u8.tmp; echo '#EXT-X-ENDLIST' >>$m3u8_name.vod.m3u8.tmp");

rename("$m3u8_name.m3u8.tmp","$m3u8_name.m3u8");
rename("$m3u8_name.vod.m3u8.tmp","$m3u8_name.vod.m3u8");
print "\x0DDone                                            \x0D"; $|++;

