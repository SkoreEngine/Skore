set(mi_sources
				src/alloc.c
				src/alloc-aligned.c
				src/alloc-posix.c
				src/arena.c
				src/bitmap.c
				src/heap.c
				src/init.c
				src/libc.c
				src/options.c
				src/os.c
				src/page.c
				src/random.c
				src/segment.c
				src/segment-map.c
				src/stats.c
				src/prim/prim.c)

add_library(mimalloc STATIC ${mi_sources})
target_include_directories(mimalloc PUBLIC include)