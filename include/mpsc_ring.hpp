#ifndef MPSC_RING_HPP_
#define MPSC_RING_HPP_

#include <rusty/macro.h>
#include <rusty/mem.h>
#include <semaphore.h>

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <vector>

namespace mpsc_ring {

namespace {
template <typename T>
class Ring {
public:
	size_t lowbit(size_t x) { return x & -x; }
	Ring(
		size_t size
	) : mask_(size - 1), ring_(size), ready_(size), head_(0), tail_(0),
		waiting_(false), sender_num_(0)
	{
		rusty_assert(size > 0, "size should be non-zero");
		rusty_assert(size == lowbit(size), "size should be the power of 2");
		int ret = sem_init(&free_slots_, false, size);
		rusty_assert(ret == 0, "sem_init failed with errno %d", errno);
	}
	~Ring() {
		// No alive sender or receiver
		size_t tail = tail_.load(/*std::memory_order_relaxed*/);
		int free_slots;
		int ret = sem_getvalue(&free_slots_, &free_slots);
		rusty_assert(ret == 0, "sem_getvalue failed with errno %d", errno);
		rusty_assert(mask_ + 1 == free_slots + (tail - head_));
		for (; head_ != tail; head_ += 1) {
			size_t i = head_ & mask_;
			ring_[i].drop();
		}
		ret = sem_destroy(&free_slots_);
		rusty_assert(ret == 0, "sem_destroy failed with errno %d", errno);
	}
	std::optional<T> recv() {
		size_t i = head_ & mask_;
		// Reading ready_ should be before reading from ring_,
		// otherwise it would be possible to read incomplete value
		if (!ready_[i].load(/*std::memory_order_acquire*/)) {
			{
				std::unique_lock<std::mutex> lock(lock_);
				// Setting waiting_ should be before the last check of ready_
				// before waiting, otherwise this is possible:
				// receiver: last check before waiting, ready_[i] is false
				// sender: ready_[i] = true
				// sender: waiting_ is false, so won't notify receiver
				// receiver: waiting_ = true
				// receiver waits forever
				waiting_.store(true, std::memory_order_seq_cst);
				cv_.wait(lock, [&]{
					return sender_num_.load(std::memory_order_seq_cst) == 0 ||
						ready_[i].load(std::memory_order_seq_cst);
				});
			}
			// We have already waked up, so memory order does not matter at all.
			waiting_.store(false/*, std::memory_order_relaxed*/);
			if (!ready_[i].load(/*std::memory_order_relaxed*/)) {
				assert(sender_num_.load(/*std::memory_order_relaxed*/) == 0);
				return std::nullopt;
			}
		}
		T v = ring_[i].into_inner();
		head_ += 1;
		// 1. Clearing ready_ should be after reading from ring_, otherwise it
		// would be possible to read value corrupted by senders.
		// 2. Clearing ready_ should be before increasing free_slots,
		// otherwise it would be possible that ready_ is set by a sender first
		// and then cleared by us.
		ready_[i].store(false, std::memory_order_seq_cst);
		int ret = sem_post(&free_slots_);
		rusty_assert(ret == 0, "sem_post failed with errno %d", errno);
		return v;
	}
	void send(T &&v) {
		for (;;) {
			int ret = sem_wait(&free_slots_);
			if (ret == 0) {
				break;
			} else if (ret == -1) {
				// if (errno == EINTR) {
				// 	continue;
				// } else {
				rusty_panic("sem_wait failed with unexpected errno %d", errno);
				// }
			}
		}
		size_t i = tail_.fetch_add(1/*, std::memory_order_relaxed*/) & mask_;
		ring_[i] = std::move(v);
		// Setting ready_ should be after writing to ring_,
		// otherwise it would be possible for reader to read incomplete value.
		ready_[i].store(true, std::memory_order_seq_cst);
		// Reading waiting_ should be after setting ready_,
		// otherwise this is possible:
		// sender: waiting_ is false, so won't notify receiver
		// receiver: ready_[i] is false
		// receiver: waiting_ = true
		// receiver: last check before waiting, ready_[i] is false
		// sender: ready_[i] = true
		// sender returns
		// receiver waits forever
		if (!waiting_.load(std::memory_order_seq_cst))
			return;
		std::unique_lock<std::mutex> lock(lock_);
		cv_.notify_one();
	}
	void inc_sender() {
		sender_num_.fetch_add(1/*, std::memory_order_relaxed*/);
	}
	void dec_sender() {
		// 1. dec_sender should be after inc_sender
		// 2. sender_num_.fetch_sub should be before reading waiting_
		size_t ori = sender_num_.fetch_sub(1/*, std::memory_order_acq_rel*/);
		if (ori > 1)
			return;
		assert(ori == 1);
		// All senders are disconnected
		if (!waiting_.load(/*std::memory_order_relaxed*/))
			return;
		std::unique_lock<std::mutex> lock(lock_);
		cv_.notify_one();
	}

private:
	size_t mask_;
	sem_t free_slots_;
	std::vector<rusty::mem::ManuallyDrop<T>> ring_;
	std::vector<std::atomic<bool>> ready_;
	size_t head_;
	std::atomic<size_t> tail_;

	std::atomic<bool> waiting_;
	std::mutex lock_;
	std::condition_variable cv_;

	std::atomic<size_t> sender_num_;
};
}

template <typename T>
class Receiver;

template <typename T>
class Sender {
public:
	Sender(const Sender &rhs) = delete;
	Sender<T> &operator=(const Sender &rhs) = delete;
	Sender(Sender &&rhs) : ring_(std::move(rhs.ring_)) {}
	Sender<T> &operator=(Sender &&rhs) {
		ring_ = std::move(rhs.ring_);
		return *this;
	}
	~Sender() {
		if (ring_.get() != nullptr) {
			ring_.get()->dec_sender();
		}
	}
	Sender<T> clone() { return Sender<T>(ring_); }
	void send(T v) {
		ring_.get()->send(std::move(v));
	}
private:
	Sender(std::shared_ptr<Ring<T>> ring) : ring_(std::move(ring)) {
		ring_.get()->inc_sender();
	}
	std::shared_ptr<Ring<T>> ring_;
	template <typename TT>
	friend std::pair<Sender<TT>, Receiver<TT>> channel(size_t size);
};

template <typename T>
class Receiver {
public:
	Receiver(const Receiver<T> &rhs) = delete;
	Receiver<T> &operator=(const Receiver<T> &rhs) = delete;
	Receiver(Receiver<T> &&rhs) : ring_(std::move(rhs.ring_)) {}
	Receiver<T> &operator=(Receiver<T> &&rhs) {
		ring_ = std::move(rhs.ring_);
		return *this;
	}
	std::optional<T> recv() {
		return ring_.get()->recv();
	}
private:
	Receiver(std::shared_ptr<Ring<T>> ring) : ring_(std::move(ring)) {}
	std::shared_ptr<Ring<T>> ring_;
	template <typename TT>
	friend std::pair<Sender<TT>, Receiver<TT>> channel(size_t size);
};

template <typename T>
std::pair<Sender<T>, Receiver<T>> channel(size_t size) {
	auto ring = std::make_shared<Ring<T>>(size);
	auto ring_clone = ring;
	return std::make_pair(
		Sender(std::move(ring)), Receiver(std::move(ring_clone))
	);
}

} // namespace mpsc_ring

#endif // MPSC_RING_HPP_
