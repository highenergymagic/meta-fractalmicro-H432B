// SPDX-License-Identifier: MIT
/* Standard V4L2 tuner exercise. Muted by default; --listen enables ten seconds of tuner audio.
 * Codec routing is separate. This tool always remutes before closing. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int request(int fd, unsigned long op, void *arg)
{
	int ret;
	do { ret = ioctl(fd, op, arg); } while (ret < 0 && errno == EINTR);
	return ret;
}

static volatile sig_atomic_t interrupted;

static void stop(int sig)
{
	(void)sig;
	interrupted = 1;
}

int main(int argc, char **argv)
{
	static const unsigned int khz[] = {87500, 90500, 99500, 107900, 87500};
	struct v4l2_capability cap = {0};
	struct v4l2_control mute = {.id = V4L2_CID_AUDIO_MUTE, .value = 1};
	int listen = argc == 2 && !strcmp(argv[1], "--listen");
	int scan = argc == 2 && !strcmp(argv[1], "--scan");
	struct sigaction action = {.sa_handler = stop};
	int fd;

	if (argc != 1 && !listen && !scan) {
		fprintf(stderr, "usage: %s [--listen|--scan]\n", argv[0]);
		return 2;
	}
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGINT, &action, NULL) ||
	    sigaction(SIGTERM, &action, NULL) ||
	    sigaction(SIGHUP, &action, NULL)) {
		perror("sigaction"); return 1;
	}
	fd = open("/dev/radio0", O_RDWR | O_CLOEXEC);
	int result = 1;

	if (fd < 0) { perror("open radio0"); return 1; }
	if (request(fd, VIDIOC_S_CTRL, &mute) < 0 ||
	    request(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		perror("radio controls"); goto out;
	}
	printf("driver=%s card=%s capabilities=%08x\n",
	       cap.driver, cap.card, cap.device_caps);
	if (!(cap.device_caps & V4L2_CAP_RADIO)) goto out;
	unsigned int count = scan ? 206 : sizeof(khz)/sizeof(khz[0]);
	for (unsigned int i = 0; i < count; ++i) {
		unsigned int frequency = scan ? 87500 + i * 100 : khz[i];
		if (interrupted) goto out;
		struct v4l2_frequency set = {
			.type = V4L2_TUNER_RADIO, .frequency = frequency * 16
		};
		struct v4l2_frequency got = {0};
		struct v4l2_tuner tuner = {0};
		if (request(fd, VIDIOC_S_FREQUENCY, &set) < 0) {
			perror("tune"); goto out;
		}
		/* Allow signal/stereo detectors to settle after tune completion. */
		struct timespec settle = {.tv_nsec = 200000000};
		nanosleep(&settle, NULL);
		if (request(fd, VIDIOC_G_FREQUENCY, &got) < 0 ||
		    request(fd, VIDIOC_G_TUNER, &tuner) < 0 ||
		    request(fd, VIDIOC_G_CTRL, &mute) < 0) {
			perror("tune/readback"); goto out;
		}
		printf("requested_khz=%u actual_khz=%u signal=%u stereo=%u mute=%d afc_rail=%d\n",
		       frequency, got.frequency / 16, tuner.signal,
		       !!(tuner.rxsubchans & V4L2_TUNER_SUB_STEREO), mute.value, tuner.afc);
		fflush(stdout);
		if (set.frequency != got.frequency || mute.value != 1) goto out;
	}
	puts("FM_MUTED_TUNE_READBACK_PASS");
	if (listen && !interrupted) {
		mute.value = 0;
		if (request(fd, VIDIOC_S_CTRL, &mute) < 0) {
			perror("unmute"); goto out;
		}
		puts("FM_LISTEN_87500_KHZ_10_SECONDS");
		fflush(stdout);
		for (unsigned int i = 0; i < 100 && !interrupted; ++i) {
			struct timespec tick = {.tv_nsec = 100000000};
			nanosleep(&tick, NULL);
		}
	}
	result = 0;
out:
	mute.value = 1;
	if (request(fd, VIDIOC_S_CTRL, &mute) < 0) result = 1;
	if (close(fd) < 0) result = 1;
	return result;
}
