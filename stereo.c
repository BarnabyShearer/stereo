#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

const char* huffman_table = "\
\xFF\xC4\x01\xA2\x00\x00\x01\x05\x01\x01\x01\x01\
\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\
\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x01\x00\x03\
\x01\x01\x01\x01\x01\x01\x01\x01\x01\x00\x00\x00\
\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\
\x0A\x0B\x10\x00\x02\x01\x03\x03\x02\x04\x03\x05\
\x05\x04\x04\x00\x00\x01\x7D\x01\x02\x03\x00\x04\
\x11\x05\x12\x21\x31\x41\x06\x13\x51\x61\x07\x22\
\x71\x14\x32\x81\x91\xA1\x08\x23\x42\xB1\xC1\x15\
\x52\xD1\xF0\x24\x33\x62\x72\x82\x09\x0A\x16\x17\
\x18\x19\x1A\x25\x26\x27\x28\x29\x2A\x34\x35\x36\
\x37\x38\x39\x3A\x43\x44\x45\x46\x47\x48\x49\x4A\
\x53\x54\x55\x56\x57\x58\x59\x5A\x63\x64\x65\x66\
\x67\x68\x69\x6A\x73\x74\x75\x76\x77\x78\x79\x7A\
\x83\x84\x85\x86\x87\x88\x89\x8A\x92\x93\x94\x95\
\x96\x97\x98\x99\x9A\xA2\xA3\xA4\xA5\xA6\xA7\xA8\
\xA9\xAA\xB2\xB3\xB4\xB5\xB6\xB7\xB8\xB9\xBA\xC2\
\xC3\xC4\xC5\xC6\xC7\xC8\xC9\xCA\xD2\xD3\xD4\xD5\
\xD6\xD7\xD8\xD9\xDA\xE1\xE2\xE3\xE4\xE5\xE6\xE7\
\xE8\xE9\xEA\xF1\xF2\xF3\xF4\xF5\xF6\xF7\xF8\xF9\
\xFA\x11\x00\x02\x01\x02\x04\x04\x03\x04\x07\x05\
\x04\x04\x00\x01\x02\x77\x00\x01\x02\x03\x11\x04\
\x05\x21\x31\x06\x12\x41\x51\x07\x61\x71\x13\x22\
\x32\x81\x08\x14\x42\x91\xA1\xB1\xC1\x09\x23\x33\
\x52\xF0\x15\x62\x72\xD1\x0A\x16\x24\x34\xE1\x25\
\xF1\x17\x18\x19\x1A\x26\x27\x28\x29\x2A\x35\x36\
\x37\x38\x39\x3A\x43\x44\x45\x46\x47\x48\x49\x4A\
\x53\x54\x55\x56\x57\x58\x59\x5A\x63\x64\x65\x66\
\x67\x68\x69\x6A\x73\x74\x75\x76\x77\x78\x79\x7A\
\x82\x83\x84\x85\x86\x87\x88\x89\x8A\x92\x93\x94\
\x95\x96\x97\x98\x99\x9A\xA2\xA3\xA4\xA5\xA6\xA7\
\xA8\xA9\xAA\xB2\xB3\xB4\xB5\xB6\xB7\xB8\xB9\xBA\
\xC2\xC3\xC4\xC5\xC6\xC7\xC8\xC9\xCA\xD2\xD3\xD4\
\xD5\xD6\xD7\xD8\xD9\xDA\xE2\xE3\xE4\xE5\xE6\xE7\
\xE8\xE9\xEA\xF2\xF3\xF4\xF5\xF6\xF7\xF8\xF9\xFA\
";

#define CHECK(x) do { \
    int retval = (x); \
    if (retval < 0) { \
        fprintf(stderr, "Runtime error: %s returned %d at %s:%d\n", #x, retval, __FILE__, __LINE__); \
        exit(retval); \
    } \
} while (0)

struct device {
    int fd;
    enum v4l2_buf_type type;
	unsigned int buffer_size;
    void *buffer;
};

static void set_format(struct device *dev, unsigned int w, unsigned int h, unsigned int format) {
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = w;
    fmt.fmt.pix.height = h;
    fmt.fmt.pix.pixelformat = format;
    fmt.fmt.pix.bytesperline = 0;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    CHECK(ioctl(dev->fd, VIDIOC_S_FMT, &fmt));
}

static void set_fps(struct device *dev, int numerator, int denominator) {
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof parm);
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = numerator;
    parm.parm.capture.timeperframe.denominator = denominator;
    CHECK(ioctl(dev->fd, VIDIOC_S_PARM, &parm));
}

static void set_control(struct device *dev, unsigned int id, int64_t val) {
    struct v4l2_queryctrl query;
	struct v4l2_ext_controls ctrls;
	struct v4l2_ext_control ctrl;

	memset(&query, 0, sizeof(query));
	query.id = id;
	CHECK(ioctl(dev->fd, VIDIOC_QUERYCTRL, &query));

	memset(&ctrls, 0, sizeof(ctrls));
	memset(&ctrl, 0, sizeof(ctrl));
	ctrls.ctrl_class = V4L2_CTRL_ID2CLASS(id);
	ctrls.count = 1;
	ctrls.controls = &ctrl;
	ctrl.id = id;
	if (query.type == V4L2_CTRL_TYPE_INTEGER64)
		ctrl.value64 = val;
	else
		ctrl.value = val;
	if(ioctl(dev->fd, VIDIOC_S_EXT_CTRLS, &ctrls)==-1) {
        if (errno == EINVAL || errno == ENOTTY) {
    		struct v4l2_control old;
    		old.id = id;
            old.value = val;
    		CHECK(ioctl(dev->fd, VIDIOC_S_CTRL, &old));
    	}
    }
}

static void open_device(struct device *dev, const char *devname) {
    struct v4l2_requestbuffers rb;
  	struct v4l2_buffer buf;

    memset(dev, 0, sizeof *dev);
    dev->buffer = NULL;
    dev->fd = open(devname, O_RDWR);

    set_format(dev, 1280, 720, V4L2_PIX_FMT_MJPEG);
    set_fps(dev, 1, 30);

    set_control(dev, 0x00980900, 210); //Brightness
    set_control(dev, 0x00980901, 0); //Contrast
    set_control(dev, 0x00980902, 42); //Saturation
    set_control(dev, 0x0098090c, 0); //Manual WB
    set_control(dev, 0x00980918, 0); //No Power Hz
    set_control(dev, 0x0098091a, 2800); //WB
    set_control(dev, 0x0098091b, 0); //Sharpness
    set_control(dev, 0x0098091c, 0); //Backlight
    set_control(dev, 0x009a0901, 1); //Manual Exposure
    set_control(dev, 0x009a0902, 20); //Exposure
    set_control(dev, 0x009a0908, 0); //No Pan
    set_control(dev, 0x009a0909, 0); //No Tilt
    set_control(dev, 0x009a090a, 0); //Focus infinity
    set_control(dev, 0x009a090c, 0); //Manual Focus
    set_control(dev, 0x009a090d, 0); //No Zoom

    memset(&rb, 0, sizeof rb);
    rb.count = 1;
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_USERPTR;
    CHECK(ioctl(dev->fd, VIDIOC_REQBUFS, &rb));

	memset(&buf, 0, sizeof buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_USERPTR;
	CHECK(ioctl(dev->fd, VIDIOC_QUERYBUF, &buf));
	CHECK(posix_memalign(&dev->buffer, getpagesize(), buf.length));
    dev->buffer_size = buf.length;
}

static void close_device(struct device *dev) {
    struct v4l2_requestbuffers rb;
    memset(&rb, 0, sizeof rb);
    rb.count = 0;
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_USERPTR;
    CHECK(ioctl(dev->fd, VIDIOC_REQBUFS, &rb));

    free(dev->buffer);
    close(dev->fd);
}

static void queue_buffer(struct device *dev) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof buf);
    buf.index = 0;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_USERPTR;
    buf.m.userptr = (unsigned long)dev->buffer;
    buf.length = dev->buffer_size;
    CHECK(ioctl(dev->fd, VIDIOC_QBUF, &buf));
}

static void save_image(struct device *dev, struct v4l2_buffer *buf, char* filename) {
    int fd;
    char* b;
    unsigned int i;
    fd = open(filename, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    b = dev->buffer;
    for(i = 2;i < buf->bytesused; i += 4) {
        b = dev->buffer;
        b += i;
        if(*b == '\xda') {
            break;
        }
    }
    i--;
    b--;
    CHECK(write(fd, dev->buffer, i));
    CHECK(write(fd, huffman_table, 420));
    CHECK(write(fd, b, buf->bytesused-i));
    close(fd);
}

static void capture(struct device *dev0, struct device *dev1, unsigned int nframes, unsigned int skip) {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct v4l2_buffer buf0;
    struct v4l2_buffer buf1;
    unsigned int i;

    memset(&buf0, 0, sizeof buf0);
    memset(&buf1, 0, sizeof buf1);
    buf0.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf0.memory = V4L2_MEMORY_USERPTR;
    buf1.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf1.memory = V4L2_MEMORY_USERPTR;

    CHECK(ioctl(dev0->fd, VIDIOC_STREAMON, &type));
    CHECK(ioctl(dev1->fd, VIDIOC_STREAMON, &type));

    for (i = 0; i < nframes; i++) {

        //Sync with one camera
        queue_buffer(dev0);
        CHECK(ioctl(dev0->fd, VIDIOC_DQBUF, &buf0));

        //Quickly read both
        queue_buffer(dev0);
        queue_buffer(dev1);
        CHECK(ioctl(dev0->fd, VIDIOC_DQBUF, &buf0));
        CHECK(ioctl(dev1->fd, VIDIOC_DQBUF, &buf1));

        if (!skip) {
            save_image(dev0, &buf0, "0.jpg");
            save_image(dev1, &buf1, "1.jpg");
        } else {
            skip--;
        }
        if (i == nframes - 1)
            continue;
    }

    CHECK(ioctl(dev0->fd, VIDIOC_STREAMOFF, &type));
    CHECK(ioctl(dev1->fd, VIDIOC_STREAMOFF, &type));
}

int main() {
    struct device dev0;
    struct device dev1;

    open_device(&dev0, "/dev/video0");
    open_device(&dev1, "/dev/video1");

    //Skip enough frames to drain the onboard buffers
    capture(&dev0, &dev1, 3, 2);

    close_device(&dev0);
    close_device(&dev1);
    return 0;
}

