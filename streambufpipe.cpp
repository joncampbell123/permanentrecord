
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#if !defined(_WIN32)/*NOT for Windows*/

#include <poll.h>

typedef struct sliding_window {
    unsigned char		*buffer;
    unsigned char		*fence;
    unsigned char		*data;
    unsigned char		*end;
} sliding_window;

static inline size_t sliding_window_alloc_length(sliding_window *sw) {
    return (size_t)(sw->fence - sw->buffer);
}

static inline size_t sliding_window_data_offset(sliding_window *sw) {
    return (size_t)(sw->data - sw->buffer);
}

static inline size_t sliding_window_data_available(sliding_window *sw) {
    return (size_t)(sw->end - sw->data);
}

static inline size_t sliding_window_can_write(sliding_window *sw) {
    return (size_t)(sw->fence - sw->end);
}

void sliding_window_free(sliding_window *sw) {
    if (sw->buffer != NULL)
        free(sw->buffer);

    sw->buffer = sw->fence = sw->data = sw->end = NULL;
}

sliding_window* sliding_window_create(size_t size) {
    sliding_window *sw = (sliding_window*)malloc(sizeof(sliding_window));
    if (!sw) return NULL;
    memset(sw,0,sizeof(*sw));
    if (size < 2) size = 2;
    sw->buffer = (unsigned char*)malloc(size);
    if (!sw->buffer) {
        sliding_window_free(sw);
        return NULL;
    }
    sw->fence = sw->buffer + size;
    sw->data = sw->end = sw->buffer;
    return sw;
}

sliding_window* sliding_window_destroy(sliding_window *sw) {
    if (sw) {
        sliding_window_free(sw);
        memset(sw,0,sizeof(*sw));
        free(sw);
    }
    return NULL;
}

size_t sliding_window_flush(sliding_window *sw) {
    size_t valid_data,ret=0;

    /* copy the valid data left back to the front of the buffer, reset data/end pointers. */
    /* NOTICE: used properly this allows fast reading and parsing of streams. over-used, and
     * performance will suffer horribly. call this function only when you need to. */

    /* if already there, do nothing */
    if (sw->data == sw->buffer) return 0;

    ret = (size_t)(sw->data - sw->buffer);
    valid_data = sliding_window_data_available(sw);
    if (valid_data > 0) memmove(sw->buffer,sw->data,valid_data);
    sw->data = sw->buffer;
    sw->end = sw->data + valid_data;

    return ret;
}

size_t sliding_window_lazy_flush(sliding_window *sw) {
    /* lazy flush: call sliding_window_flush() only if more than half the entire buffer has
     * been consumed. a caller that wants a generally-optimal streaming buffer policy would
     * call this instead of duplicating code to check and call all over the place. */
    size_t threshhold = ((size_t)(sw->fence - sw->buffer)) >> 1;
    if ((sw->data+threshhold) >= sw->end && sliding_window_data_offset(sw) >= (threshhold/2))
        return sliding_window_flush(sw);

    return 0;
}

ssize_t sliding_window_refill_from_fd(sliding_window *sw,int fd,size_t max) {
    size_t cw;
    ssize_t rd;
    if (fd < 0) return 0;
    cw = sliding_window_can_write(sw);
    if (max == 0) max = sliding_window_alloc_length(sw);
    if (max > cw) max = cw;
    if (max == 0) return 0;
    rd = read(fd,sw->end,max);
    if (rd == 0 || (rd < 0 && errno == EPIPE)) { /* whoops. disconnect */ return (ssize_t)(-1); }
    if (rd < 0) return 0;
    if ((size_t)rd > max) rd = (ssize_t)max;
    sw->end += rd;
    return rd;
}

ssize_t sliding_window_empty_to_fd(sliding_window *sw,int fd,size_t max) {
    size_t cr = sliding_window_data_available(sw);
    ssize_t wd;
    if (fd < 0) return 0;
    if (max == 0) max = cr;
    else if (cr > max) cr = max;
    if (cr == 0) return 0;
    wd = write(fd,sw->data,cr);
    if (wd == 0 || (wd < 0 && errno == EPIPE)) { /* whoops. disconnect */ return (ssize_t)(-1); }
    if (wd < 0) return 0;
    if ((size_t)wd > cr) wd = (ssize_t)cr;
    sw->data += wd;
    return wd;
}

int sliding_window_is_sane(sliding_window *sw) {
    if (sw == NULL) return 0;

    /* if any of the pointers are NULL, */
    if (sw->buffer == NULL || sw->data == NULL || sw->fence == NULL || sw->end == NULL)
        return 0;

    return	(sw->buffer <= sw->data) &&
        (sw->data <= sw->end) &&
        (sw->end <= sw->fence) &&
        (sw->buffer != sw->fence);
}

static int fd_non_block(int fd) {
    int x;
    x = fcntl(fd,F_GETFL);
    if (x < 0) return -1;
    if (fcntl(fd,F_SETFL,x | O_NONBLOCK) < 0) return -1;
    return 0;
}

int main() { /* TODO: command line options */
    unsigned int sleep_period = 10000;

    /* make STDIN and STDOUT non-blocking */
    if (fd_non_block(0/*STDIN*/) != 0 || fd_non_block(1/*STDOUT*/) != 0)
        return 1;

    sliding_window *sio = sliding_window_create(64*1024*1024);
    if (sio == NULL)
        return 1;

    /* NTS: If the process we are piping to closes it's end, we'll terminate
     *      with SIGPIPE. No cleanup needed. */
    while (1) {
        if (sliding_window_data_available(sio) >= 4096)
            sliding_window_lazy_flush(sio);
        else
            sliding_window_flush(sio);

        ssize_t rd = sliding_window_refill_from_fd(sio,0/*STDIN*/,0);
        if (rd < 0 && sliding_window_data_available(sio) == 0)
            break;

        ssize_t wd = sliding_window_empty_to_fd(sio,1/*STDOUT*/,0);
        if (wd < 0)
            break;

        /* check for output hangup even if we never have anything to write.
         * this program can be left dangling if output closes and the input
         * never sent in any data such as dvbsnoop without a tuner command. */
        if (rd == 0 && wd == 0) {
            struct pollfd p;
            memset(&p,0,sizeof(p));
            p.fd = 1;
            p.events = POLLHUP;
            if (poll(&p,1,0) > 0) {
                if (p.revents != 0) {
                    fprintf(stderr,"Output hung up.\n");
                    break;
                }
            }
        }

        if (rd == 0 && sliding_window_can_write(sio) == 0)
            fprintf(stderr,"WARNING: Potential incoming data loss, buffer overrun\n");

        if (rd == 0 && wd == 0) {
            usleep(sleep_period);
            if (sleep_period < 250000)
                sleep_period += 10000;
        }
        else {
            sleep_period = 10000;
        }
    }

    sio = sliding_window_destroy(sio);
    return 0;
}
#else /*WIN32*/
int main() {
	fprintf(stderr,"Not for Windows\n");
	return 1;
}
#endif /*WIN32*/

