#pragma once
// API keys from the untracked firmware/secrets.h; without it the build still works and shows "No API key".
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
