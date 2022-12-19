

#ifndef MOBIUS_COMMON_H
#define MOBIUS_COMMON_H

#include <stdint.h>
#include <iostream>
#include <chrono>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t  s8;



enum class
Mobius_Error {
	#define ENUM_VALUE(handle) handle,
	#include "error_types.incl"
	#undef ENUM_VALUE
};

inline const char *
name(Mobius_Error type) {
	switch(type) {
	#define ENUM_VALUE(handle) case Mobius_Error::handle: return #handle;
	#include "error_types.incl"
	#undef ENUM_VALUE
	}
	return "";
}

inline void
error_print() {}

inline void
warning_print() {}

#if defined(MOBIUS_ERROR_STREAMS)
#include <sstream>

extern std::stringstream global_error_stream;
extern std::stringstream global_warning_stream;

template<typename T, typename... V> inline void
error_print(T value, V... tail) {
	global_error_stream << value;
	error_print(tail...);
}	

template<typename T, typename... V> inline void
warning_print(T value, V... tail) {
	global_warning_stream << value;
	warning_print(tail...);
}

inline void
mobius_error_exit() {
	error_print("\n\n");
	throw 1;
}

#else

template<typename T, typename... V> inline void
error_print(T value, V... tail) {
	std::cerr << value;
	error_print(tail...);
}	

template<typename T, typename... V> inline void
warning_print(T value, V... tail) {
	std::cout << value;
	warning_print(tail...);
}

inline void
mobius_error_exit() {
	error_print("\n\n");
	exit(1);
	// TODO: For internal errors we should provide a stack trace here (if that is feasible).
}
#endif

inline void
begin_error(Mobius_Error type) {
	error_print("ERROR (", name(type), "): ");
}

template<typename... V> inline void
fatal_error(Mobius_Error type, V... tail) {
	begin_error(type);
	error_print(tail...);
	mobius_error_exit();
}

template<typename... V> inline void
fatal_error(V... tail) {
	error_print(tail...);
	mobius_error_exit();
}


//NOTE: we use the intrin header for __rdtsc(); The intrinsic is in different headers for different compilers. If you compile with a different compiler than what is already set up you have to add in some lines below.
#if defined(__GNUC__) || defined(__GNUG__)
	#include <x86intrin.h>
#elif defined(_MSC_VER)
	#include <intrin.h>
#endif

struct 
Timer {
	std::chrono::time_point<std::chrono::high_resolution_clock> begin;
	s64 cycle_begin;
	
	Timer() {
		begin = std::chrono::high_resolution_clock::now();
		cycle_begin = (s64)__rdtsc();
	}
	
	s64 get_cycles() {
		return (s64)__rdtsc() - cycle_begin;
	}
	
	s64 get_milliseconds() {
		auto   end = std::chrono::high_resolution_clock::now();
		return std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
	}
};

#endif //MOBIUS_COMMON_H