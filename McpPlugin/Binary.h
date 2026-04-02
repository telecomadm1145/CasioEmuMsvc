/*

	Binary Serialization Utility
	Copyright (C) 2024 telecomadm1145

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#pragma once
// #include "Config.hpp"
#include <fstream>
#include <stdexcept>
#include <vector>
#include <map>
#include <type_traits>
#include <concepts>
#include <cstring>
#pragma warning(push)
#pragma warning(disable : 4267)

template <class T>
concept trivial = std::is_trivial<T>::value;
template <class T>
concept trivial_pair = trivial<typename T::first_type> && trivial<typename T::second_type>;
template <class T>
concept BinaryData = trivial<T> || trivial_pair<T>;
template <class T>
concept BinaryClass =
	requires(T& t, std::ostream& os, std::istream& is) {
		{ t.Write(os) } -> std::same_as<void>;
		{ t.Read(is) } -> std::same_as<void>;
	};
template <class T>
using ContainerChild = std::remove_cvref_t<decltype(*std::declval<T>().cbegin())>;
template <class T>
concept BinaryVector =
	requires(T v, typename T::value_type& val, size_t sz) {
		// 需要大小操作
		{ v.size() } -> std::convertible_to<std::size_t>;
		{ v.reserve(sz) } -> std::same_as<void>;

		// 需要只读迭代器
		{ v.cbegin() } -> std::convertible_to<typename T::const_iterator>;
		{ v.cend() } -> std::convertible_to<typename T::const_iterator>;

		{ v.push_back(val) } -> std::same_as<void>;
	};
template <class T>
concept BinaryMap =
	requires(T m) {
		// 需要键值对类型
		typename T::key_type;
		typename T::mapped_type;
		typename T::value_type;

		// 需要只读迭代器
		{ m.cbegin() } -> std::convertible_to<typename T::const_iterator>;
		{ m.cend() } -> std::convertible_to<typename T::const_iterator>;
	};

// #define _BIN_DBG

/// <summary>
/// 提供结构化的二进制与stl的转换
/// </summary>
class Binary {
public:
	static void Write(std::ostream& stm, const BinaryData auto& dat) {
		stm.write((char*)&dat, sizeof(dat));
#ifdef _BIN_DBG
		stm.flush();
#endif //  _BIN_DBG
	}
	static void Read(std::istream& stm, BinaryData auto& dat) {
		stm.read((char*)&dat, sizeof(dat));
	}
	static void Write(std::ostream& stm, const BinaryClass auto& cls) {
		cls.Write(stm);
	}
	static void Read(std::istream& stm, BinaryClass auto& cls) {
		cls.Read(stm);
	}
	static void Read(std::istream& stm, BinaryVector auto& vec) {
		using ContainerChild = ::ContainerChild<decltype(vec)>;
		unsigned long long size = 0;
		Read(stm, size);
		if (size > 1ULL << 48) {
			__debugbreak();
		}
		vec.reserve(size);
		for (size_t i = 0; i < size; i++) {
			if (stm.eof())
				return;
			ContainerChild data{};
			Read(stm, data);
			vec.push_back(data);
		}
	}
	static void Write(std::ostream& stm, const BinaryVector auto& vec) {
		unsigned long long sz = vec.size();
		Write(stm, sz);
		for (const auto& data : vec) {
			Write(stm, data);
			sz--;
		}
		if (sz != 0)
			__debugbreak();
	}
	static void Read(std::istream& stm, BinaryMap auto& map) {
		using ContainerChild = ::ContainerChild<decltype(map)>;
		unsigned long long size = 0;
		Read(stm, size);
		if (size > 1ULL << 48) {
			__debugbreak();
		}
		for (size_t i = 0; i < size; i++) {
			if (stm.eof())
				return;
			std::remove_cvref_t<typename ContainerChild::first_type> key{};
			Read(stm, key);
			std::remove_cvref_t<typename ContainerChild::second_type> val{};
			Read(stm, val);
			map[key] = val;
		}
	}
	static void Write(std::ostream& stm, const BinaryMap auto& map) {
		unsigned long long sz = map.size();
		Write(stm, sz);
		for (const auto& kv : map) {
			Write(stm, kv.first);
			Write(stm, kv.second);
			sz--;
		}
		if (sz != 0)
			__debugbreak();
	}
};
#pragma warning(pop)