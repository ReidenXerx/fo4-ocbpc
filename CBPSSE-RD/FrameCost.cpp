// fo4-anatomy (2026-11-06): the engine's frame cost by part (FrameCost.h).
#include "PCH.h"
#include "FrameCost.h"

#include <chrono>
#include <cstdio>
#include <mutex>

namespace FrameCost
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		const char* kNames[kParts] = { "scan", "aim", "sound", "colliders", "mouth", "tubes", "hash", "loaded actors",
			"ticks", "bones", "physics", "skirt", "canal", "other", "config" };

		struct Window
		{
			double us[kParts] = {};
			double total = 0.0, worst = 0.0;
			size_t frames = 0, actors = 0;
		};

		Clock::time_point mark;
		double frame[kParts] = {};
		Window running, last;
		std::mutex lock;   // Text() runs on the health check's thread

		std::string Format(const Window& w)
		{
			if (!w.frames)
				return "not measured yet";
			char head[160];
			std::snprintf(head, sizeof(head), "%.2f ms per frame on average, worst %.2f ms, over %zu frames, %.1f actors; ",
				w.total / w.frames / 1000.0, w.worst / 1000.0, w.frames, (double)w.actors / w.frames);
			std::string out = head;
			// the parts, biggest first, those of 0.01 ms or more
			int order[kParts];
			for (int i = 0; i < kParts; i++)
				order[i] = i;
			std::sort(order, order + kParts, [&](int a, int b) { return w.us[a] > w.us[b]; });
			bool any = false;
			for (int i : order) {
				const double ms = w.us[i] / w.frames / 1000.0;
				if (ms < 0.01)
					continue;
				char part[64];
				std::snprintf(part, sizeof(part), "%s%s %.2f", any ? ", " : "", kNames[i], ms);
				out += part;
				any = true;
			}
			if (!any)
				out += "every part under 0.01";
			return out + " ms";
		}
	}

	void BeginFrame()
	{
		for (double& d : frame)
			d = 0.0;
		mark = Clock::now();
	}

	void Lap(Part a_part)
	{
		const auto now = Clock::now();
		frame[a_part] += std::chrono::duration<double, std::micro>(now - mark).count();
		mark = now;
	}

	void EndFrame(size_t a_actors)
	{
		double total = 0.0;
		for (double d : frame)
			total += d;
		std::string line;
		{
			std::lock_guard<std::mutex> l(lock);
			for (int i = 0; i < kParts; i++)
				running.us[i] += frame[i];
			running.total += total;
			running.worst = (std::max)(running.worst, total);
			running.frames++;
			running.actors += a_actors;
			if (running.frames >= 1800) {   // ~30 s at 60 fps, ~12 s at 144
				last = running;
				running = Window{};
				line = Format(last);
			}
		}
		if (!line.empty())
			spdlog::info("engine frame: {}", line);
	}

	std::string Text()
	{
		std::lock_guard<std::mutex> l(lock);
		return Format(running.frames >= 300 || !last.frames ? running : last);
	}
}
