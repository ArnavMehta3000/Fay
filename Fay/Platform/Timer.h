#pragma once
#include "Common/Types.h"
#include <chrono>

namespace fay
{
	class Timer
	{
	public:
		using Clock     = std::chrono::high_resolution_clock;
		using TimePoint = Clock::time_point;
		using Duration  = std::chrono::duration<f64>;

	public:
		Timer()
			: m_start(Clock::now())
			, m_last(m_start)
			, m_current(m_start)
		{
		}

		f32 Tick()
		{
			m_current = Clock::now();
			m_dt      = m_current - m_last;
			m_last    = m_current;
			m_elapsed = m_current - m_start;
			++m_frameCount;

			return DeltaTime();
		}

		void Reset()
		{
			m_start      = Clock::now();
			m_last       = m_start;
			m_current    = m_start;
			m_dt         = Duration{ 0.0 };
			m_elapsed    = Duration{ 0.0 };
			m_frameCount = 0;
			m_smoothFPS  = 0.0f;
			m_fpsAccum   = 0.0;
			m_fpsFrames  = 0;
			m_fpsTimer   = Duration{ 0.0 };
		}

		void UpdateFPSCounter(f64 intervalSeconds = 0.5)
		{
			m_fpsAccum += m_dt.count();
			++m_fpsFrames;

			if (m_fpsAccum >= intervalSeconds)
			{
				m_smoothFPS = static_cast<f32>(m_fpsFrames / m_fpsAccum);
				m_fpsAccum  = 0.0;
				m_fpsFrames = 0;
			}
		}

		[[nodiscard]] inline f32 DeltaTime()      const { return static_cast<f32>(m_dt.count());      }
		[[nodiscard]] inline f64 DeltaTimeF64()   const { return m_dt.count();                        }
		[[nodiscard]] inline f32 ElapsedTime()    const { return static_cast<f32>(m_elapsed.count()); }
		[[nodiscard]] inline f64 ElapsedTimeF64() const { return m_elapsed.count();                   }
		[[nodiscard]] inline f32 FPS()            const { return m_smoothFPS;                         }		
		[[nodiscard]] inline u64 FrameCount()     const { return m_frameCount;                        }
	private:
		TimePoint m_start;
		TimePoint m_last;
		TimePoint m_current;

		Duration  m_dt{ 0.0 };
		Duration  m_elapsed{ 0.0 };

		u64       m_frameCount = 0;

		f32       m_smoothFPS  = 0.0f;
		f64       m_fpsAccum   = 0.0;
		u32       m_fpsFrames  = 0;
		Duration  m_fpsTimer{ 0.0 };
	};
}