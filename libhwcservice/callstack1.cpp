#include "callstack1.h"
#include <utils/CallStack.h>
extern "C" {
    void dump_stack01(void)
   {
	ALOGE("hdcpd call stack dump");
        //android::CallStack stack;
        //stack.update();
        //stack.dump();
       android::CallStack cs("hdcpd");
       cs.update();
       cs.log("hdcpd", ANDROID_LOG_ERROR, "");
   }
}
