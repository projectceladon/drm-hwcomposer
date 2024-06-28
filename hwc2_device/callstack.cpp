#include "callstack.h"
#include <utils/CallStack.h>
extern "C" {
    void dump_stack02(void)
   {
	ALOGE("hdcpd call stack dump");
        //android::CallStack stack;
        //stack.update();
        //stack.dump();
       android::CallStack cs("INTEL-MESA");
       cs.update();
       cs.log("INTEL-MESA", ANDROID_LOG_ERROR, "");
   }
}
