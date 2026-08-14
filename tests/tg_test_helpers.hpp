#pragma once

// Shared helpers for the GoogleTest-based core tests.
// Keeps the historical EXPECT_CONTAINS(str, sub) assertion working on top of
// plain EXPECT_TRUE (no gmock dependency), with a readable failure message.

#include <gtest/gtest.h>

#include <string>

#define EXPECT_CONTAINS(str, sub)                                            \
    do {                                                                      \
        const std::string _tg_s = (str);                                      \
        const std::string _tg_sub = (sub);                                    \
        EXPECT_TRUE(_tg_s.find(_tg_sub) != std::string::npos)                 \
            << "\"" << _tg_s << "\" does not contain \"" << _tg_sub << "\"";  \
    } while (0)
