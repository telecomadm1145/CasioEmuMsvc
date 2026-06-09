#pragma once
#include <atomic>
#include <concepts>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>

template <typename T>
concept Movable = std::is_move_constructible_v<T> && std::is_move_assignable_v<T>;

template <Movable T>
class ConcurrentObject {
private:
	mutable std::shared_mutex m_mutex;
	mutable std::atomic<std::thread::id> m_owning_thread{{}};
	mutable std::atomic<int> m_recursive_count{0};
	std::unique_ptr<T> m_storage;

public:
	// 完美转发构造函数，仅接受可移动类型
	template <typename... Args>
		requires std::constructible_from<T, Args...>
	explicit ConcurrentObject(Args&&... args)
		: m_storage(std::make_unique<T>(std::forward<Args>(args)...)) {}

	// 防止拷贝，允许移动
	ConcurrentObject(const ConcurrentObject&) = delete;
	ConcurrentObject& operator=(const ConcurrentObject&) = delete;
	ConcurrentObject(ConcurrentObject&&) noexcept = default;
	ConcurrentObject& operator=(ConcurrentObject&&) noexcept = default;

	template <bool IsConst>
	class LockGuard {
	private:
		ConcurrentObject& m_parent;

		// 根据 IsConst 选择正确的指针和引用类型
		using StorageType = std::conditional_t<IsConst, const T, T>;
		using PointerType = std::conditional_t<IsConst, const T*, T*>;
		using ReferenceType = std::conditional_t<IsConst, const T&, T&>;

	public:
		explicit LockGuard(ConcurrentObject& parent) : m_parent(parent) {
			std::thread::id current_thread = std::this_thread::get_id();

			if (m_parent.m_owning_thread.load() == current_thread) {
				// 递归进入
				m_parent.m_recursive_count++;
				return;
			}

			// 根据 const 性质选择合适的锁
			m_parent.m_mutex.lock();

			m_parent.m_owning_thread.store(current_thread);
			m_parent.m_recursive_count = 1;
		}

		~LockGuard() {
			if (m_parent.m_recursive_count == 1) {
				m_parent.m_owning_thread.store({});
				m_parent.m_mutex.unlock();
			}

			if (m_parent.m_recursive_count > 0) {
				--m_parent.m_recursive_count;
			}
		}

		// 仅允许移动，禁止拷贝
		LockGuard(const LockGuard&) = delete;
		LockGuard& operator=(const LockGuard&) = delete;
		LockGuard(LockGuard&&) noexcept = default;
		LockGuard& operator=(LockGuard&&) noexcept = default;

		// 成员访问操作符
		PointerType operator->() const {
			return m_parent.m_storage.get();
		}

		// 解引用操作符
		ReferenceType operator*() const {
			return *m_parent.m_storage;
		}
	};

	// 获取可变锁定对象
	[[nodiscard]] auto get()
	{
		return LockGuard<false>(*this);
	}

	// 获取只读锁定对象
	[[nodiscard]] auto get_const() const {
		return LockGuard<true>(*(ConcurrentObject*)(this));
	}
};
