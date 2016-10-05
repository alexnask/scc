About this project
------------------

SCC is the Shoddy C Compiler.  
The goal is to initially write a multiplatform self hosting C11 compiler with a LLVM backend.  
Subsequently, the aim is to write an x86-64 backend.  

Currently, no extention support is planned (even if they are common in other compilers).  

No C standard library will be provided (although certain parts like threading could be added for convenience, since no practically no libc implements them).  
Instead, ww will be relying on existing libc implementations (mainly Glibc and the MSVC libc but also newlib and musl).  

The compiler is an educational project and will never be stable or production ready by any means.  
The source code will be as simple as possible and well documented so that it may help other people interested in compiler development and the inner workings of C.  

Some high level optimizations such as inlining and loop unrolling may be available in the future, after the static analyzer is completed.  

Authors
-------

@shamanas (Alexandros Naskos)
