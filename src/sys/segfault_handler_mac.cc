#include <mach/mach.h>
#include "core/core.h"
#include "sys/segfault_handler_mac.h"

using namespace dreavm::sys;

// POSIX signal handlers, for whatever reason, don't seem to be invoked for
// segmentation faults on OSX when running the application under lldb / gdb.
// Handling the original Mach exception seems to be the only way to capture
// them.

// http://web.mit.edu/darwin/src/modules/xnu/osfmk/man/exc_server.html
extern "C" boolean_t exc_server(mach_msg_header_t *request_msg,
                                mach_msg_header_t *reply_msg);

// http://web.mit.edu/darwin/src/modules/xnu/osfmk/man/catch_exception_raise.html
extern "C" kern_return_t catch_exception_raise(
    mach_port_t exception_port, mach_port_t thread, mach_port_t task,
    exception_type_t exception, exception_data_t code,
    mach_msg_type_number_t code_count) {
  // get exception state
  mach_msg_type_number_t state_count = x86_EXCEPTION_STATE64_COUNT;
  x86_exception_state64_t exc_state;
  if (thread_get_state(thread, x86_EXCEPTION_STATE64,
                       reinterpret_cast<thread_state_t>(&exc_state),
                       &state_count) != KERN_SUCCESS) {
    return KERN_FAILURE;
  }

  // get thread state
  state_count = x86_THREAD_STATE64_COUNT;
  x86_thread_state64_t thread_state;
  if (thread_get_state(thread, x86_THREAD_STATE64,
                       reinterpret_cast<thread_state_t>(&thread_state),
                       &state_count) != KERN_SUCCESS) {
    return KERN_FAILURE;
  }

  uintptr_t rip = thread_state.__rip;
  uintptr_t fault_addr = exc_state.__faultvaddr;
  bool handled = SegfaultHandler::instance().HandleAccessFault(rip, fault_addr);
  if (!handled) {
    return KERN_FAILURE;
  }

  // reset thread state
  if (thread_set_state(thread, x86_THREAD_STATE64,
                       reinterpret_cast<thread_state_t>(&thread_state),
                       state_count) != KERN_SUCCESS) {
    return KERN_FAILURE;
  }

  return KERN_SUCCESS;
}

SegfaultHandler &SegfaultHandler::instance() {
  static SegfaultHandlerMac instance;
  return instance;
}

SegfaultHandlerMac::~SegfaultHandlerMac() {
  task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS, 0,
                           EXCEPTION_DEFAULT, 0);
  mach_port_deallocate(mach_task_self(), listen_port_);
}

bool SegfaultHandlerMac::Init() {
  // allocate port to listen for exceptions
  if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
                         &listen_port_) != KERN_SUCCESS) {
    return false;
  }

  // http://web.mit.edu/darwin/src/modules/xnu/osfmk/man/mach_port_insert_right.html
  if (mach_port_insert_right(mach_task_self(), listen_port_, listen_port_,
                             MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) {
    return false;
  }

  // filter out any exception other than EXC_BAD_ACCESS
  // http://web.mit.edu/darwin/src/modules/xnu/osfmk/man/task_set_exception_ports.html
  if (task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS,
                               listen_port_, EXCEPTION_DEFAULT,
                               MACHINE_THREAD_STATE) != KERN_SUCCESS) {
    return false;
  }

  // launch thread to listen for exceptions
  thread_ = std::thread([this] { ThreadEntry(); });
  thread_.detach();

  return true;
}

void SegfaultHandlerMac::ThreadEntry() {
  while (true) {
    struct {
      mach_msg_header_t head;
      mach_msg_body_t msgh_body;
      char data[1024];
    } msg;
    struct {
      mach_msg_header_t head;
      char data[1024];
    } reply;

    // wait for a message on the exception port
    mach_msg_return_t ret =
        mach_msg(&msg.head, MACH_RCV_MSG | MACH_RCV_LARGE, 0, sizeof(msg),
                 listen_port_, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (ret != MACH_MSG_SUCCESS) {
      LOG_INFO("mach_msg receive failed with %d %s", ret,
               mach_error_string(ret));
      break;
    }

    // call exc_server, which will call back into catch_exception_raise
    exc_server(&msg.head, &reply.head);

    // send the reply
    ret = mach_msg(&reply.head, MACH_SEND_MSG, reply.head.msgh_size, 0,
                   MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (ret != MACH_MSG_SUCCESS) {
      LOG_INFO("mach_msg send failed with %d %s", ret, mach_error_string(ret));
      break;
    }
  }
}
