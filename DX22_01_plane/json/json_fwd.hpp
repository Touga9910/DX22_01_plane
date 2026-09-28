#pragma once

#ifndef INCLUDE_NLOHMANN_JSON_FWD_HPP_
#define INCLUDE_NLOHMANN_JSON_FWD_HPP_

// Forward declarations matching the bundled nlohmann/json 3.12.0 build.
// This keeps the 25k-line implementation header out of broadly included APIs.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifndef NLOHMANN_JSON_VERSION_MAJOR
#define NLOHMANN_JSON_VERSION_MAJOR 3
#define NLOHMANN_JSON_VERSION_MINOR 12
#define NLOHMANN_JSON_VERSION_PATCH 0
#endif

#ifndef JSON_DIAGNOSTICS
#define JSON_DIAGNOSTICS 0
#endif
#ifndef JSON_DIAGNOSTIC_POSITIONS
#define JSON_DIAGNOSTIC_POSITIONS 0
#endif
#ifndef JSON_USE_LEGACY_DISCARDED_VALUE_COMPARISON
#define JSON_USE_LEGACY_DISCARDED_VALUE_COMPARISON 0
#endif

#if JSON_DIAGNOSTICS
#define NLOHMANN_JSON_ABI_TAG_DIAGNOSTICS _diag
#else
#define NLOHMANN_JSON_ABI_TAG_DIAGNOSTICS
#endif
#if JSON_DIAGNOSTIC_POSITIONS
#define NLOHMANN_JSON_ABI_TAG_DIAGNOSTIC_POSITIONS _dp
#else
#define NLOHMANN_JSON_ABI_TAG_DIAGNOSTIC_POSITIONS
#endif
#if JSON_USE_LEGACY_DISCARDED_VALUE_COMPARISON
#define NLOHMANN_JSON_ABI_TAG_LEGACY_DISCARDED_VALUE_COMPARISON _ldvcmp
#else
#define NLOHMANN_JSON_ABI_TAG_LEGACY_DISCARDED_VALUE_COMPARISON
#endif

#define NLOHMANN_JSON_ABI_TAGS_CONCAT_EX(a, b, c) json_abi ## a ## b ## c
#define NLOHMANN_JSON_ABI_TAGS_CONCAT(a, b, c) \
	NLOHMANN_JSON_ABI_TAGS_CONCAT_EX(a, b, c)
#define NLOHMANN_JSON_ABI_TAGS \
	NLOHMANN_JSON_ABI_TAGS_CONCAT( \
		NLOHMANN_JSON_ABI_TAG_DIAGNOSTICS, \
		NLOHMANN_JSON_ABI_TAG_LEGACY_DISCARDED_VALUE_COMPARISON, \
		NLOHMANN_JSON_ABI_TAG_DIAGNOSTIC_POSITIONS)

#define NLOHMANN_JSON_NAMESPACE_VERSION_CONCAT_EX(major, minor, patch) \
	_v ## major ## _ ## minor ## _ ## patch
#define NLOHMANN_JSON_NAMESPACE_VERSION_CONCAT(major, minor, patch) \
	NLOHMANN_JSON_NAMESPACE_VERSION_CONCAT_EX(major, minor, patch)
#ifndef NLOHMANN_JSON_NAMESPACE_NO_VERSION
#define NLOHMANN_JSON_NAMESPACE_NO_VERSION 0
#endif
#if NLOHMANN_JSON_NAMESPACE_NO_VERSION
#define NLOHMANN_JSON_NAMESPACE_VERSION
#else
#define NLOHMANN_JSON_NAMESPACE_VERSION \
	NLOHMANN_JSON_NAMESPACE_VERSION_CONCAT( \
		NLOHMANN_JSON_VERSION_MAJOR, \
		NLOHMANN_JSON_VERSION_MINOR, \
		NLOHMANN_JSON_VERSION_PATCH)
#endif

#define NLOHMANN_JSON_NAMESPACE_CONCAT_EX(a, b) a ## b
#define NLOHMANN_JSON_NAMESPACE_CONCAT(a, b) \
	NLOHMANN_JSON_NAMESPACE_CONCAT_EX(a, b)
#ifndef NLOHMANN_JSON_NAMESPACE
#define NLOHMANN_JSON_NAMESPACE \
	nlohmann::NLOHMANN_JSON_NAMESPACE_CONCAT( \
		NLOHMANN_JSON_ABI_TAGS, NLOHMANN_JSON_NAMESPACE_VERSION)
#endif
#ifndef NLOHMANN_JSON_NAMESPACE_BEGIN
#define NLOHMANN_JSON_NAMESPACE_BEGIN \
	namespace nlohmann { inline namespace NLOHMANN_JSON_NAMESPACE_CONCAT( \
		NLOHMANN_JSON_ABI_TAGS, NLOHMANN_JSON_NAMESPACE_VERSION) {
#endif
#ifndef NLOHMANN_JSON_NAMESPACE_END
#define NLOHMANN_JSON_NAMESPACE_END } }
#endif

NLOHMANN_JSON_NAMESPACE_BEGIN

template<typename T = void, typename SFINAE = void>
struct adl_serializer;

template<
	template<typename U, typename V, typename... Args> class ObjectType = std::map,
	template<typename U, typename... Args> class ArrayType = std::vector,
	class StringType = std::string,
	class BooleanType = bool,
	class NumberIntegerType = std::int64_t,
	class NumberUnsignedType = std::uint64_t,
	class NumberFloatType = double,
	template<typename U> class AllocatorType = std::allocator,
	template<typename T, typename SFINAE = void> class JSONSerializer = adl_serializer,
	class BinaryType = std::vector<std::uint8_t>,
	class CustomBaseClass = void>
class basic_json;

template<typename RefStringType>
class json_pointer;

using json = basic_json<>;

template<class Key, class T, class IgnoredLess, class Allocator>
struct ordered_map;

using ordered_json = basic_json<nlohmann::ordered_map>;

NLOHMANN_JSON_NAMESPACE_END

// The bundled json.hpp is the amalgamated header and defines these helpers
// unconditionally. Keep this lightweight header composable with it on MSVC.
#undef NLOHMANN_JSON_NAMESPACE_END
#undef NLOHMANN_JSON_NAMESPACE_BEGIN
#undef NLOHMANN_JSON_NAMESPACE
#undef NLOHMANN_JSON_NAMESPACE_CONCAT
#undef NLOHMANN_JSON_NAMESPACE_CONCAT_EX
#undef NLOHMANN_JSON_NAMESPACE_VERSION
#undef NLOHMANN_JSON_NAMESPACE_VERSION_CONCAT
#undef NLOHMANN_JSON_NAMESPACE_VERSION_CONCAT_EX
#undef NLOHMANN_JSON_ABI_TAGS
#undef NLOHMANN_JSON_ABI_TAGS_CONCAT
#undef NLOHMANN_JSON_ABI_TAGS_CONCAT_EX
#undef NLOHMANN_JSON_ABI_TAG_LEGACY_DISCARDED_VALUE_COMPARISON
#undef NLOHMANN_JSON_ABI_TAG_DIAGNOSTIC_POSITIONS
#undef NLOHMANN_JSON_ABI_TAG_DIAGNOSTICS
#undef NLOHMANN_JSON_VERSION_PATCH
#undef NLOHMANN_JSON_VERSION_MINOR
#undef NLOHMANN_JSON_VERSION_MAJOR

#endif
