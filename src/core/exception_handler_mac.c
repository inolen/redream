#include <mach/mach.h>
#include <pthread.h>
#include "core/core.h"
#include "core/exception_handler.h"

/* POSIX signal handlers, for whatever reason, don't seem to be invoked for
   segmentation faults on OSX when running the application under lldb / gdb.
   handling the original Mach exception seems to be the only way to capture
   them
   https://llvm.org/bugs/show_bug.cgi?id=22868 */

static const exception_mask_t exception_mask =
    EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION;

static int installed;
static mach_port_t listen_port;

static void copy_state_to(x86_thread_state64_t *src, struct thread_state *dst) {
  dst->rax = src->__rax;
  dst->rcx = src->__rcx;
  dst->rdx = src->__rdx;
  dst->rbx = src->__rbx;
  dst->rsp = src->__rsp;
  dst->rbp = src->__rbp;
  dst->rsi = src->__rsi;
  dst->rdi = src->__rdi;
  dst->r8 = src->__r8;
  dst->r9 = src->__r9;
  dst->r10 = src->__r10;
  dst->r11 = src->__r11;
  dst->r12 = src->__r12;
  dst->r13 = src->__r13;
  dst->r14 = src->__r14;
  dst->r15 = src->__r15;
  dst->rip = src->__rip;
}

static void copy_state_from(struct thread_state *src,
                            x86_thread_state64_t *dst) {
  dst->__rax = src->rax;
  dst->__rcx = src->rcx;
  dst->__rdx = src->rdx;
  dst->__rbx = src->rbx;
  dst->__rsp = src->rsp;
  dst->__rbp = src->rbp;
  dst->__rsi = src->rsi;
  dst->__rdi = src->rdi;
  dst->__r8 = src->r8;
  dst->__r9 = src->r9;
  dst->__r10 = src->r10;
  dst->__r11 = src->r11;
  dst->__r12 = src->r12;
  dst->__r13 = src->r13;
  dst->__r14 = src->r14;
  dst->__r15 = src->r15;
  dst->__rip = src->rip;
}

/* http://web.mit.edu/darwin/src/modules/xnu/osfmk/man/exc_server.html */
boolean_t exc_server(mach_msg_header_t *request_msg,
                     mach_msg_header_t *reply_msg);

/* http://web.mit.edu/darwin/src/modules/xnu/osfmk/man/catch_exception_raise.html
 */
kern_return_t catch_exception_raise(mach_port_t exception_port,
                                    mach_port_t thread, mach_port_t task,
                                    enum exception_type exception,
                                    exception_data_t code,
                                    mach_msg_type_number_t code_count) {
  /* get exception state */
  mach_msg_type_number_t state_count = x86_EXCEPTION_STATE64_COUNT;
  x86_exception_state64_t exc_state;
  if (thread_get_state(thread, x86_EXCEPTION_STATE64,
                       (thread_state_t)&exc_state,
                       &state_count) != KERN_SUCCESS) {
    return KERN_FAILURE;
  }

  /* get thread state */
  state_count = x86_THREAD_STATE64_COUNT;
  x86_thread_state64_t thread_state;
  if (thread_get_state(thread, x86_THREAD_STATE64,
                       (thread_state_t)&thread_state,
                       &state_count) != KERN_SUCCESS) {
    return KERN_FAILURE;
  }

  /* convert mach exception to internal exception */
  struct exception_state ex;
  ex.type = exception == EXC_BAD_ACCESS ? EX_ACCESS_VIOLATION
                                        : EX_INVALID_INSTRUCTION;
  ex.fault_addr = exc_state.__faultvaddr;
  ex.pc = thread_state.__rip;
  copy_state_to(&thread_state, &ex.thread_state);

  /* call exception handler, letting it potentially update the thread state */
  int handled = exception_handler_handle(&ex);
  if (!handled) {
    return KERN_FAILURE;
  }

  /* copy internal thread state back to mach thread state and restore */
  copy_state_from(&ex.thread_state, &thread_state);

  if (thread_set_state(thread, x86_THREAD_STATE64,
                       (thread_state_t)&thread_state,
                       state_count) != KERN_SUCCESS) {
    return KERN_FAILURE;
  }

  return KERN_SUCCESS;
}

static void *mach_exception_thread(void *data) {
  while (1) {
    struct {
      mach_msg_header_t head;
      mach_msg_body_t msgh_body;
      char data[1024];
    } msg;
    struct {
      mach_msg_header_t head;
      char data[1024];
    } reply;

    /* wait for a message on the exception port */
    mach_msg_return_t ret =
        mach_msg(&msg.head, MACH_RCV_MSG | MACH_RCV_LARGE, 0, sizeof(msg),
                 listen_port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (ret != MACH_MSG_SUCCESS) {
      LOG_INFO("mach_msg receive failed with %d %s", ret,
               mach_error_string(ret));
      break;
    }

    /* call exc_server, which will call back into catch_exception_raise */
    exc_server(&msg.head, &reply.head);

    /* send the reply */
    ret = mach_msg(&reply.head, MACH_SEND_MSG, reply.head.msgh_size, 0,
                   MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (ret != MACH_MSG_SUCCESS) {
      LOG_INFO("mach_msg send failed with %d %s", ret, mach_error_string(ret));
      break;
    }
  }

  return NULL;
}

int exception_handler_install_platform() {
  if (installed) {
    return 1;
  }

  /* allocate port to listen for exceptions */
  if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
                         &listen_port) != KERN_SUCCESS) {
    return 0;
  }

  /* http://web.mit.edu/darwin/src/modules/xnu/osfmk/man/mach_port_insert_right.html
   */
  if (mach_port_insert_right(mach_task_self(), listen_port, listen_port,
                             MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) {
    return 0;
  }

  /* filter out any exception other than EXC_BAD_ACCESS */
  /* http://web.mit.edu/darwin/src/modules/xnu/osfmk/man/task_set_exception_ports.html
   */
  if (task_set_exception_ports(mach_task_self(), exception_mask, listen_port,
                               EXCEPTION_DEFAULT,
                               MACHINE_THREAD_STATE) != KERN_SUCCESS) {
    return 0;
  }

  /* launch thread to listen for exceptions */
  pthread_attr_t attr;
  pthread_t thread;

  if (pthread_attr_init(&attr) != 0) {
    return 0;
  }

  if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
    return 0;
  }

  if (pthread_create(&thread, &attr, &mach_exception_thread, NULL) != 0) {
    return -1;
  }

  pthread_attr_destroy(&attr);

  installed = 1;

  return 1;
}

void exception_handler_uninstall_platform() {
  task_set_exception_ports(mach_task_self(), exception_mask, 0,
                           EXCEPTION_DEFAULT, 0);

  mach_port_deallocate(mach_task_self(), listen_port);
}
