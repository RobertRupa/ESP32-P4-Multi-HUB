#pragma once

struct portMUX_TYPE {
    int unused = 0;
};

#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))