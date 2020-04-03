This patch is needed to make dvbsnoop give up and quit if for any reason
it is not getting any data from the DVB device.

This can happen if dvbsnoop is run automatically and the script was unable
to tune the device, in which case no data comes from the device.

Without the patch, dvbsnoop will wait indefinitely until terminated which
is undesirable when a simple retry will ensure a capture.
