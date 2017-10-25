#include "core/core.h"
#include "core/filesystem.h"
#include "core/thread.h"
#include "guest/dreamcast.h"
#include "guest/serial/serial.h"
#include "guest/sh4/sh4.h"

enum {
  STATE_LOADING,
  STATE_RUNNING,
  STATE_SHUTDOWN,
};

static void sys_write();

typedef void (*syscall_cb)();

static syscall_cb syscalls[] = {
    NULL,      /* exit */
    NULL,      /* fstat */
    sys_write, /* write */
    NULL,      /* read */
    NULL,      /* open */
    NULL,      /* close */
    NULL,      /* create */
    NULL,      /* link */
    NULL,      /* unlink */
    NULL,      /* chdir */
    NULL,      /* chmod */
    NULL,      /* lseek */
    NULL,      /* time */
    NULL,      /* state */
    NULL,      /* utime */
    NULL,      /* unknown */
    NULL,      /* opendir */
    NULL,      /* closedir */
    NULL,      /* readdir */
    NULL,      /* readsectors */
    NULL,      /* gdbpacket */
    NULL,      /* rewinddir */
};

/*
 * serial device to communicate with dcload
 */

/* the serial device has an infinitely-large queue for incoming and outgoing
   data, providing a higher-level interface for transmitting data on top of
   the raw putchar and getchar callbacks */
#define BLOCK_SIZE 4096

struct data_block {
  uint8_t data[BLOCK_SIZE];
  int read;
  int write;
  struct list_node it;
};

static mutex_t dev_mutex;
static struct list dev_readq;
static struct list dev_writeq;

static void queue_putchar(struct list *list, int c) {
  mutex_lock(dev_mutex);

  struct data_block *block = list_last_entry(list, struct data_block, it);

  if (!block || block->write >= BLOCK_SIZE) {
    struct data_block *next = calloc(1, sizeof(*block));
    list_add_after_entry(list, block, next, it);
    block = next;
  }

  block->data[block->write++] = (uint8_t)c;

  mutex_unlock(dev_mutex);
}

static int queue_getchar(struct list *list) {
  int c = -1;

  mutex_lock(dev_mutex);

  struct data_block *block = list_first_entry(list, struct data_block, it);

  if (block && block->read < block->write) {
    c = (int)block->data[block->read++];

    if (block->read >= BLOCK_SIZE) {
      list_remove(list, &block->it);
      free(block);
    }
  }

  mutex_unlock(dev_mutex);

  return c;
}

/* called on the emulation thread when the scif is ready to receive another
   character */
static int dev_getchar(void *userdata) {
  return queue_getchar(&dev_writeq);
}

/* called on the emulation thread when the scif is transmitting another
   character */
static void dev_putchar(void *userdata, int c) {
  queue_putchar(&dev_readq, c);
}

static void dev_read_raw(void *ptr, int size) {
  uint8_t *data = ptr;

  while (size) {
    int c = queue_getchar(&dev_readq);

    /* TODO use a condition variable instead of spinning / locking constantly */

    /* block until a char is available */
    if (c == -1) {
      continue;
    }

    *(data++) = c;
    size--;
  }
}

static void dev_write_raw(const void *ptr, int size) {
  const uint8_t *data = ptr;

  while (size--) {
    queue_putchar(&dev_writeq, *(data++));
  }
}

static void dev_read_blob(void *ptr, int size) {
  uint8_t *data = ptr;

  char type;
  dev_read_raw(&type, 1);
  CHECK_EQ(type, 'U');

  int n;
  dev_read_raw(&n, 4);
  dev_read_raw(data, n);

  int sum;
  dev_read_raw(&sum, 1);

  char ok = 'G';
  dev_write_raw(&ok, 1);
}

static void dev_write_checked(const void *ptr, int size) {
  uint8_t tmp[4];
  CHECK_LE(size, (int)sizeof(tmp));

  dev_write_raw(ptr, size);
  dev_read_raw(tmp, size);

  CHECK(memcmp(ptr, tmp, size) == 0);
}

/*
 * dcload syscalls
 */
static void sys_write() {
  int fd;
  dev_read_raw(&fd, 4);

  int n;
  dev_read_raw(&n, 4);

  char *data = malloc(n);
  dev_read_blob(data, n);

  int res = write(fd, data, n);
  dev_write_checked(&res, 4);

  free(data);
}

/*
 * dcload commands
 */
char checksum(const void *ptr, int size) {
  const uint8_t *data = ptr;
  char sum = 0;
  while (size--) {
    sum ^= *(data++);
  }
  return sum;
}

static void run_code(uint32_t addr) {
  /* send over serial if */
  {
    char cmd = 'A';
    int console = 1;

    dev_write_checked(&cmd, 1);
    dev_write_checked(&addr, 4);
    dev_write_checked(&console, 4);
  }

  /* parse syscall responses */
  {
    while (1) {
      char cmd;
      dev_read_raw(&cmd, 1);

      if (!cmd) {
        break;
      }

      CHECK(cmd >= 0 && cmd < (int)ARRAY_SIZE(syscalls));

      syscall_cb cb = syscalls[(int)cmd];
      CHECK_NOTNULL(cb, "run_code unexpected syscall=%d", cmd);
      cb();
    }
  }
}

static void load_code(uint32_t addr, const char *path) {
  uint8_t *bin = NULL;
  int bin_size = 0;

  /* load file */
  {
    FILE *fp = fopen(path, "rb");
    CHECK(fp);

    fseek(fp, 0, SEEK_END);
    bin_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    bin = malloc(bin_size);
    int n = (int)fread(bin, 1, bin_size, fp);
    CHECK_EQ(n, bin_size);

    fclose(fp);
  }

  /* send over serial if */
  {
    /* write load binary command */
    char cmd = 'B';
    dev_write_checked(&cmd, 1);
    dev_write_checked(&addr, 4);
    dev_write_checked(&bin_size, 4);

    /* write payload */
    char type = 'U';
    char sum = checksum(bin, bin_size);
    dev_write_raw(&type, 1);
    dev_write_checked(&bin_size, 4);
    dev_write_raw(bin, bin_size);
    dev_write_raw(&sum, 1);
    dev_read_raw(&type, 1);
    CHECK_EQ(type, 'G');
  }

  free(bin);
}

/*
 * main program
 */
static volatile int state;
static struct dreamcast *dc;
static thread_t dc_thread;

static void *dc_main(void *data) {
  const char *dcload_path = data;
  struct serial *serial = NULL;
  int res = 0;

  dc = dc_create(NULL);
  CHECK_NOTNULL(dc);

  serial = serial_create(dc, NULL, dev_getchar, dev_putchar);
  dc_add_serial_device(dc, serial);

  res = dc_load(dc, dcload_path);
  CHECK(res);

  state = STATE_RUNNING;

  while (state == STATE_RUNNING) {
    dc_tick(dc, 1000);
  }

  serial_destroy(serial);
  serial = NULL;

  dc_destroy(dc);
  dc = NULL;

  return NULL;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    LOG_INFO("reload /path/to/dcload-serial.cdi /path/to/test.bin ...");
    return EXIT_FAILURE;
  }

  /* set application directory */
  char appdir[PATH_MAX];
  char userdir[PATH_MAX];
  int r = fs_userdir(userdir, sizeof(userdir));
  CHECK(r);
  snprintf(appdir, sizeof(appdir), "%s" PATH_SEPARATOR ".redream", userdir);
  fs_set_appdir(appdir);

  /* startup machine */
  dev_mutex = mutex_create();
  CHECK_NOTNULL(dev_mutex);

  dc_thread = thread_create(&dc_main, NULL, argv[1]);
  CHECK_NOTNULL(dc_thread);

  /* wait for it to initialize */
  while (state == STATE_LOADING) {
  }

  /* run each binary */
  const uint32_t code_addr = 0x8c010000;

  for (int i = 2; i < argc; i++) {
    load_code(code_addr, argv[i]);
    run_code(code_addr);
  }

  /* shutdown machine */
  void *result;

  state = STATE_SHUTDOWN;

  thread_join(dc_thread, &result);
  dc_thread = NULL;

  mutex_destroy(dev_mutex);
  dev_mutex = NULL;

  return EXIT_SUCCESS;
}
