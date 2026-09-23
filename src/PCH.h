#pragma once

#define DEBUG

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "RE/Offsets.Ext.h"

#pragma warning(push)
#ifdef DEBUG
#	include <spdlog/sinks/basic_file_sink.h>
#else
#	include <spdlog/sinks/msvc_sink.h>
#endif
#pragma warning(pop)

using namespace std::literals;

namespace logger = SKSE::log;
namespace stle
{
	using namespace SKSE::stl;

	template <class T>
	void write_thunk_call(std::uintptr_t a_src)
	{
		auto& trampoline = SKSE::GetTrampoline();
		SKSE::AllocTrampoline(14);
		T::func = trampoline.write_call<5>(a_src, T::thunk);
	}

	template <class F, std::size_t idx, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[0] };
		T::func = vtbl.write_vfunc(idx, T::thunk);
	}
}
#define DLLEXPORT __declspec(dllexport)

#define RELOCATION_OFFSET(a_se, a_ae) \
	REL::VariantOffset(static_cast<std::size_t>(a_se), static_cast<std::size_t>(a_ae), 0).offset()

#include "Version.h"

