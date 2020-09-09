#pragma once

#include <nanovg.h>
#include <string>
#include <switch.h>

namespace tsl {

    void Initialize();
    void Exit();

    class Context {
        NVGcontext *impl;
        int slot;

      public:
        Context();
        ~Context();

        operator NVGcontext *() {
            return impl;
        }
    };

    Event *GetVsyncEvent();

}
