#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "dexode/EventBus.hpp"
#include "dexode/eventbus/internal/AsyncCallbackVector.h"

namespace dexode::eventbus::strategy
{

class Protected
{
public:
	Protected() = default;
	~Protected() = default;

	Protected(const Protected&) = delete;
	Protected(Protected&&) = delete;

	Protected& operator=(Protected&&) = delete;
	Protected& operator=(const Protected&) = delete;

	template <typename Event>
	void post(const Event& event)
	{
		std::shared_lock readLock{_mutex};

		using Vector = eventbus::internal::AsyncCallbackVector<Event>;
		auto found = _callbacks.find(eventbus::internal::event_id<Event>());
		if(found == _callbacks.end())
		{
			return; // no such notifications
		}

		std::unique_ptr<eventbus::internal::CallbackVector>& vector = found->second;
		assert(dynamic_cast<Vector*>(vector.get()));
		auto* callbacks = static_cast<Vector*>(vector.get());

		for(const auto& element : callbacks->container)
		{
			element.second(event);
		}
	}

	template <typename Event>
	void postpone(Event&& event)
	{
		{
			std::unique_lock writeLock{_mutex};
			_eventQueue.push_back(
				[this, event = std::forward<Event>(event)]() { post<Event>(event); });
		}
		_eventWaiting.notify_one();
	}

	std::size_t processLimit(std::size_t maxCountOfEvents);

	std::size_t getPostponeEventCount() const
	{
		std::shared_lock lock{_mutex};
		return _eventQueue.size();
	}

	bool wait();
	bool waitFor(std::chrono::milliseconds timeout);

	template <class Event>
	void listen(const std::uint32_t listenerID, std::function<void(const Event&)>&& callback)
	{
		using Vector = eventbus::internal::AsyncCallbackVector<Event>;

		std::unique_lock writeLock{_mutex};
		auto eventListeners = _callbacks.find(eventbus::internal::event_id<Event>());
		if(eventListeners == _callbacks.cend())
		{
			eventListeners = _callbacks.emplace_hint(
				eventListeners, eventbus::internal::event_id<Event>(), std::make_unique<Vector>());
		}
		assert(dynamic_cast<Vector*>(eventListeners->second.get()));
		auto* vectorImpl = static_cast<Vector*>(eventListeners->second.get());
		vectorImpl->add(listenerID, std::forward<std::function<void(const Event&)>>(callback));
	}

	void unlistenAll(std::uint32_t listenerID);

	template <typename Event>
	void unlisten(const std::uint32_t listenerID)
	{
		std::unique_lock writeLock{_mutex}; // TODO locking already locked mutex
		auto found = _callbacks.find(eventbus::internal::event_id<Event>());
		if(found != _callbacks.end())
		{
			found->second->remove(listenerID);
		}
	}

private:
	std::map<eventbus::internal::event_id_t, std::unique_ptr<eventbus::internal::CallbackVector>>
		_callbacks;
	mutable std::shared_mutex _mutex;

	std::mutex _waitMutex;
	std::condition_variable _eventWaiting;
	std::deque<std::function<void()>> _eventQueue;
};

} // namespace dexode::eventbus::strategy
