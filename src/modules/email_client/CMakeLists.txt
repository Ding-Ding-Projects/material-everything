add_library(material_email_client_module
    EmailClientModule.hpp
    EmailClientModule.cpp
)
target_include_directories(material_email_client_module PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(material_email_client_module PUBLIC cxx_std_20)
