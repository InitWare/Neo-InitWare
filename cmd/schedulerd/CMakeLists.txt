add_executable(schedulerd app/app.cc js/fd.cc js/js.cc js/promise.cc js/timer.cc
    scheduler/txgen.cc scheduler/scheduler.cc schedulerd.cc)
target_link_libraries(schedulerd quickjs ${KQ_LIB})
target_compile_options(schedulerd PUBLIC "-Wno-c99-designator")