# Portable core shared by every platform app. Keep it free of OS headers.
add_library(sw2_core STATIC
    src/Protocol.cpp
)
target_include_directories(sw2_core PUBLIC include)
target_compile_features(sw2_core PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(sw2_core PRIVATE /W3 /permissive- /utf-8)
else()
    target_compile_options(sw2_core PRIVATE -Wall -Wextra)
endif()
