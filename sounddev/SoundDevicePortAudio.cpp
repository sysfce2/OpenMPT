/*
 * SoundDevicePortAudio.cpp
 * ------------------------
 * Purpose: PortAudio sound device driver class.
 * Notes  : (currently none)
 * Authors: Olivier Lapicque
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"

#include "SoundDevice.h"

#include "SoundDevicePortAudio.h"

#include "../common/misc_util.h"


#ifdef MPT_WITH_PORTAUDIO
#if defined(MODPLUG_TRACKER) && MPT_COMPILER_MSVC
#include "../include/portaudio/src/common/pa_debugprint.h"
#endif
#if MPT_OS_WINDOWS
#include <shellapi.h>
#endif
#endif


OPENMPT_NAMESPACE_BEGIN


namespace SoundDevice {


#ifdef MPT_WITH_PORTAUDIO

#ifdef MPT_ALL_LOGGING
#define PALOG(x) MPT_LOG(LogDebug, "PortAudio", x)
#else
#define PALOG(x) MPT_DO { } MPT_WHILE_0
#endif



CPortaudioDevice::CPortaudioDevice(SoundDevice::Info info, SoundDevice::SysInfo sysInfo)
	: SoundDevice::Base(info, sysInfo)
	, m_StatisticPeriodFrames(0)
{
	mpt::ustring internalID = GetDeviceInternalID();
	if(internalID == U_("WASAPI-Default"))
	{
		m_DeviceIsDefault = true;
		m_DeviceIndex = Pa_GetHostApiInfo(Pa_HostApiTypeIdToHostApiIndex(paWASAPI))->defaultOutputDevice;
	} else
	{
		m_DeviceIsDefault = false;
		m_DeviceIndex = ConvertStrTo<PaDeviceIndex>(internalID);
	}
	m_HostApiType = Pa_GetHostApiInfo(Pa_GetDeviceInfo(m_DeviceIndex)->hostApi)->type;
	MemsetZero(m_StreamParameters);
	MemsetZero(m_InputStreamParameters);
#if MPT_OS_WINDOWS
	MemsetZero(m_WasapiStreamInfo);
#endif // MPT_OS_WINDOWS
	m_Stream = 0;
	m_StreamInfo = 0;
	m_CurrentFrameBuffer = nullptr;
	m_CurrentFrameBufferInput = nullptr;
	m_CurrentFrameCount = 0;
	m_CurrentRealLatency = 0.0;
}


CPortaudioDevice::~CPortaudioDevice()
{
	Close();
}


bool CPortaudioDevice::InternalOpen()
{
	MemsetZero(m_StreamParameters);
	MemsetZero(m_InputStreamParameters);
	m_Stream = 0;
	m_StreamInfo = 0;
	m_CurrentFrameBuffer = 0;
	m_CurrentFrameBufferInput = 0;
	m_CurrentFrameCount = 0;
	m_StreamParameters.device = m_DeviceIndex;
	if(m_StreamParameters.device == -1) return false;
	m_StreamParameters.channelCount = m_Settings.Channels;
	if(m_Settings.sampleFormat.IsFloat())
	{
		if(m_Settings.sampleFormat.GetBitsPerSample() != 32) return false;
		m_StreamParameters.sampleFormat = paFloat32;
	} else
	{
		switch(m_Settings.sampleFormat.GetBitsPerSample())
		{
		case 8: m_StreamParameters.sampleFormat = paInt8; break;
		case 16: m_StreamParameters.sampleFormat = paInt16; break;
		case 24: m_StreamParameters.sampleFormat = paInt24; break;
		case 32: m_StreamParameters.sampleFormat = paInt32; break;
		default: return false; break;
		}
	}
	m_StreamParameters.suggestedLatency = m_Settings.Latency;
	m_StreamParameters.hostApiSpecificStreamInfo = NULL;
	unsigned long framesPerBuffer = static_cast<long>(m_Settings.UpdateInterval * m_Settings.Samplerate);
	if(m_HostApiType == paWASAPI)
	{
#if MPT_OS_WINDOWS
		MemsetZero(m_WasapiStreamInfo);
		m_WasapiStreamInfo.size = sizeof(PaWasapiStreamInfo);
		m_WasapiStreamInfo.hostApiType = paWASAPI;
		m_WasapiStreamInfo.version = 1;
		if(m_Settings.BoostThreadPriority)
		{
			m_WasapiStreamInfo.flags |= paWinWasapiThreadPriority;
			if(GetAppInfo().BoostedThreadMMCSSClassVista == U_(""))
			{
				m_WasapiStreamInfo.threadPriority = eThreadPriorityNone;
			} else if(GetAppInfo().BoostedThreadMMCSSClassVista == U_("Audio"))
			{
				m_WasapiStreamInfo.threadPriority = eThreadPriorityAudio;
			} else if(GetAppInfo().BoostedThreadMMCSSClassVista == U_("Capture"))
			{
				m_WasapiStreamInfo.threadPriority = eThreadPriorityCapture;
			} else if(GetAppInfo().BoostedThreadMMCSSClassVista == U_("Distribution"))
			{
				m_WasapiStreamInfo.threadPriority = eThreadPriorityDistribution;
			} else if(GetAppInfo().BoostedThreadMMCSSClassVista == U_("Games"))
			{
				m_WasapiStreamInfo.threadPriority = eThreadPriorityGames;
			} else if(GetAppInfo().BoostedThreadMMCSSClassVista == U_("Playback"))
			{
				m_WasapiStreamInfo.threadPriority = eThreadPriorityPlayback;
			} else if(GetAppInfo().BoostedThreadMMCSSClassVista == U_("Pro Audio"))
			{
				m_WasapiStreamInfo.threadPriority = eThreadPriorityProAudio;
			} else if(GetAppInfo().BoostedThreadMMCSSClassVista == U_("Window Manager"))
			{
				m_WasapiStreamInfo.threadPriority = eThreadPriorityWindowManager;
			} else
			{
				m_WasapiStreamInfo.threadPriority = eThreadPriorityNone;
			}
			m_StreamParameters.hostApiSpecificStreamInfo = &m_WasapiStreamInfo;
		}
#endif // MPT_OS_WINDOWS
		if(m_Settings.ExclusiveMode)
		{
			m_Flags.NeedsClippedFloat = false;
#if MPT_OS_WINDOWS
			m_WasapiStreamInfo.flags |= paWinWasapiExclusive | paWinWasapiExplicitSampleFormat;
			m_StreamParameters.hostApiSpecificStreamInfo = &m_WasapiStreamInfo;
#endif // MPT_OS_WINDOWS
		} else
		{
			m_Flags.NeedsClippedFloat = GetSysInfo().IsOriginal();
		}
	} else if(m_HostApiType == paWDMKS)
	{
		m_Flags.NeedsClippedFloat = false;
		framesPerBuffer = paFramesPerBufferUnspecified; // let portaudio choose
	} else if(m_HostApiType == paMME)
	{
		m_Flags.NeedsClippedFloat = GetSysInfo().IsOriginal();
	} else if(m_HostApiType == paDirectSound)
	{
		m_Flags.NeedsClippedFloat = GetSysInfo().IsOriginal();
	} else
	{
		m_Flags.NeedsClippedFloat = false;
	}
	m_InputStreamParameters = m_StreamParameters;
	if(!HasInputChannelsOnSameDevice())
	{
		m_InputStreamParameters.device = static_cast<PaDeviceIndex>(m_Settings.InputSourceID);
	}
	m_InputStreamParameters.channelCount = m_Settings.InputChannels;
	if(Pa_IsFormatSupported((m_Settings.InputChannels > 0) ? &m_InputStreamParameters : NULL, &m_StreamParameters, m_Settings.Samplerate) != paFormatIsSupported)
	{
		if(m_HostApiType == paWASAPI)
		{
			if(m_Settings.ExclusiveMode)
			{
#if MPT_OS_WINDOWS
				m_WasapiStreamInfo.flags &= ~paWinWasapiExplicitSampleFormat;
				m_StreamParameters.hostApiSpecificStreamInfo = &m_WasapiStreamInfo;
#endif // MPT_OS_WINDOWS
				if(Pa_IsFormatSupported((m_Settings.InputChannels > 0) ? &m_InputStreamParameters : NULL, &m_StreamParameters, m_Settings.Samplerate) != paFormatIsSupported)
				{
					return false;
				}
			} else
			{
				if(!GetSysInfo().IsWine && GetSysInfo().WindowsVersion.IsAtLeast(mpt::Windows::Version::Win7))
				{ // retry with automatic stream format conversion (i.e. resampling)
	#if MPT_OS_WINDOWS
					m_WasapiStreamInfo.flags |= paWinWasapiAutoConvert;
					m_StreamParameters.hostApiSpecificStreamInfo = &m_WasapiStreamInfo;
	#endif // MPT_OS_WINDOWS
					if(Pa_IsFormatSupported((m_Settings.InputChannels > 0) ? &m_InputStreamParameters : NULL, &m_StreamParameters, m_Settings.Samplerate) != paFormatIsSupported)
					{
						return false;
					}
				} else
				{
					return false;
				}
			}
		} else
		{
			return false;
		}
	}
	PaStreamFlags flags = paNoFlag;
	if(m_Settings.DitherType == 0)
	{
		flags |= paDitherOff;
	}
	if(Pa_OpenStream(&m_Stream, (m_Settings.InputChannels > 0) ? &m_InputStreamParameters : NULL, &m_StreamParameters, m_Settings.Samplerate, framesPerBuffer, flags, StreamCallbackWrapper, reinterpret_cast<void*>(this)) != paNoError) return false;
	m_StreamInfo = Pa_GetStreamInfo(m_Stream);
	if(!m_StreamInfo)
	{
		Pa_CloseStream(m_Stream);
		m_Stream = 0;
		return false;
	}
	return true;
}


bool CPortaudioDevice::InternalClose()
{
	if(m_Stream)
	{
		const SoundDevice::BufferAttributes bufferAttributes = GetEffectiveBufferAttributes();
		Pa_AbortStream(m_Stream);
		Pa_CloseStream(m_Stream);
		if(Pa_GetDeviceInfo(m_StreamParameters.device)->hostApi == Pa_HostApiTypeIdToHostApiIndex(paWDMKS))
		{
			Pa_Sleep(mpt::saturate_round<long>(bufferAttributes.Latency * 2.0 * 1000.0 + 0.5)); // wait for broken wdm drivers not closing the stream immediatly
		}
		MemsetZero(m_StreamParameters);
		MemsetZero(m_InputStreamParameters);
		m_StreamInfo = 0;
		m_Stream = 0;
		m_CurrentFrameCount = 0;
		m_CurrentFrameBuffer = 0;
		m_CurrentFrameBufferInput = 0;
	}
	return true;
}


bool CPortaudioDevice::InternalStart()
{
	return Pa_StartStream(m_Stream) == paNoError;
}


void CPortaudioDevice::InternalStop()
{
	Pa_StopStream(m_Stream);
}


void CPortaudioDevice::InternalFillAudioBuffer()
{
	if(m_CurrentFrameCount == 0) return;
	SourceLockedAudioReadPrepare(m_CurrentFrameCount, mpt::saturate_cast<std::size_t>(mpt::saturate_round<int64>(m_CurrentRealLatency * m_StreamInfo->sampleRate)));
	SourceLockedAudioRead(m_CurrentFrameBuffer, m_CurrentFrameBufferInput, m_CurrentFrameCount);
	m_StatisticPeriodFrames.store(m_CurrentFrameCount);
	SourceLockedAudioReadDone();
}


int64 CPortaudioDevice::InternalGetStreamPositionFrames() const
{
	if(Pa_IsStreamActive(m_Stream) != 1) return 0;
	return static_cast<int64>(Pa_GetStreamTime(m_Stream) * m_StreamInfo->sampleRate);
}


SoundDevice::BufferAttributes CPortaudioDevice::InternalGetEffectiveBufferAttributes() const
{
	SoundDevice::BufferAttributes bufferAttributes;
	bufferAttributes.Latency = m_StreamInfo->outputLatency;
	bufferAttributes.UpdateInterval = m_Settings.UpdateInterval;
	bufferAttributes.NumBuffers = 1;
	if(m_HostApiType == paWASAPI && m_Settings.ExclusiveMode)
	{
		// WASAPI exclusive mode streams only account for a single period of latency in PortAudio
		// (i.e. the same way as Steinerg ASIO defines latency).
		// That does not match our definition of latency, repair it.
		bufferAttributes.Latency *= 2.0;
	}
	return bufferAttributes;
}


bool CPortaudioDevice::OnIdle()
{
	if(!IsPlaying())
	{
		return false;
	}
	if(m_Stream)
	{
		if(m_HostApiType == paWDMKS)
		{
			// Catch timeouts in PortAudio threading code that cause the thread to exit.
			// Restore our desired playback state by resetting the whole sound device.
			if(Pa_IsStreamActive(m_Stream) <= 0)
			{
				// Hung state tends to be caused by an overloaded system.
				// Sleeping too long would freeze the UI,
				// but at least sleep a tiny bit of time to let things settle down.
				const SoundDevice::BufferAttributes bufferAttributes = GetEffectiveBufferAttributes();
				Pa_Sleep(mpt::saturate_round<long>(bufferAttributes.Latency * 2.0 * 1000.0 + 0.5));
				RequestReset();
				return true;
			}
		}
	}
	return false;
}


SoundDevice::Statistics CPortaudioDevice::GetStatistics() const
{
	MPT_TRACE_SCOPE();
	SoundDevice::Statistics result;
	result.InstantaneousLatency = m_CurrentRealLatency;
	result.LastUpdateInterval = 1.0 * m_StatisticPeriodFrames / m_Settings.Samplerate;
	result.text = mpt::ustring();
#if MPT_OS_WINDOWS
	if(m_HostApiType == paWASAPI)
	{
		if(m_Settings.ExclusiveMode)
		{
			if(m_StreamParameters.hostApiSpecificStreamInfo && (m_WasapiStreamInfo.flags & paWinWasapiExplicitSampleFormat))
			{
				result.text += U_("Exclusive stream.");
			} else
			{
				result.text += U_("Exclusive stream with sample format conversion.");
			}
		} else
		{
			if(m_StreamParameters.hostApiSpecificStreamInfo && (m_WasapiStreamInfo.flags & paWinWasapiAutoConvert))
			{
				result.text += U_("WASAPI stream resampling.");
			} else
			{
				result.text += U_("No resampling.");
			}
		}
	}
#endif // MPT_OS_WINDOWS
	return result;
}


SoundDevice::Caps CPortaudioDevice::InternalGetDeviceCaps()
{
	SoundDevice::Caps caps;
	caps.Available = true;
	caps.CanUpdateInterval = true;
	caps.CanSampleFormat = true;
	caps.CanExclusiveMode = false;
	caps.CanBoostThreadPriority = false;
	caps.CanUseHardwareTiming = false;
	caps.CanChannelMapping = false;
	caps.CanInput = false;
	caps.HasNamedInputSources = false;
	caps.CanDriverPanel = false;
	caps.HasInternalDither = true;
	caps.DefaultSettings.sampleFormat = SampleFormatFloat32;
	const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(m_DeviceIndex);
	if(deviceInfo)
	{
		caps.DefaultSettings.Latency = deviceInfo->defaultLowOutputLatency;
	}
	if(HasInputChannelsOnSameDevice())
	{
		caps.CanInput = true;
		caps.HasNamedInputSources = false;
	} else
	{
		caps.CanInput = (EnumerateInputOnlyDevices(m_HostApiType).size() > 0);
		caps.HasNamedInputSources = caps.CanInput;
	}
	if(m_HostApiType == paWASAPI)
	{
		caps.CanBoostThreadPriority = true;
		caps.CanDriverPanel = true;
		caps.DefaultSettings.sampleFormat = SampleFormatFloat32;
		if(m_DeviceIsDefault)
		{
			caps.CanExclusiveMode = false;
			caps.DefaultSettings.Latency = 0.030;
			caps.DefaultSettings.UpdateInterval = 0.010;
		} else
		{
			caps.CanExclusiveMode = true;
			if(deviceInfo)
			{
				// PortAudio WASAPI returns the device period as latency
				caps.DefaultSettings.Latency = deviceInfo->defaultHighOutputLatency * 2.0;
				caps.DefaultSettings.UpdateInterval = deviceInfo->defaultHighOutputLatency;
			}
		}
	} else if(m_HostApiType == paWDMKS)
	{
		caps.CanUpdateInterval = false;
		caps.DefaultSettings.sampleFormat = SampleFormatInt32;
	} else if(m_HostApiType == paDirectSound)
	{
		if(GetSysInfo().IsOriginal())
		{
			caps.DefaultSettings.sampleFormat = SampleFormatFloat32;
		} else
		{
			caps.DefaultSettings.sampleFormat = SampleFormatInt16;
		}
	} else if(m_HostApiType == paMME)
	{
		if(GetSysInfo().IsOriginal())
		{
			caps.DefaultSettings.sampleFormat = SampleFormatFloat32;
		} else
		{
			caps.DefaultSettings.sampleFormat = SampleFormatInt16;
		}
	} else if(m_HostApiType == paASIO)
	{
		caps.DefaultSettings.sampleFormat = SampleFormatInt32;
	}
	if(m_HostApiType == paDirectSound)
	{
		if(GetSysInfo().IsOriginal())
		{
			caps.HasInternalDither = false;
		}
	} else if(m_HostApiType == paMME)
	{
		if(GetSysInfo().IsOriginal())
		{
			caps.HasInternalDither = false;
		}
	} else if(m_HostApiType == paJACK)
	{
		caps.HasInternalDither = false;
	} else if(m_HostApiType == paWASAPI)
	{
		caps.HasInternalDither = false;
	}
	return caps;
}


SoundDevice::DynamicCaps CPortaudioDevice::GetDeviceDynamicCaps(const std::vector<uint32> &baseSampleRates)
{
	SoundDevice::DynamicCaps caps;
	PaDeviceIndex device = m_DeviceIndex;
	if(device == paNoDevice)
	{
		return caps;
	}
	for(std::size_t n = 0; n<baseSampleRates.size(); n++)
	{
		PaStreamParameters StreamParameters;
		MemsetZero(StreamParameters);
		StreamParameters.device = device;
		StreamParameters.channelCount = 2;
		StreamParameters.sampleFormat = paInt16;
		if(m_HostApiType == paWASAPI)
		{
			StreamParameters.sampleFormat = paFloat32;
		}
		StreamParameters.suggestedLatency = 0.0;
		StreamParameters.hostApiSpecificStreamInfo = NULL;
		if(Pa_IsFormatSupported(NULL, &StreamParameters, baseSampleRates[n]) == paFormatIsSupported)
		{
			caps.supportedSampleRates.push_back(baseSampleRates[n]);
			if(!((m_HostApiType == paWASAPI) && m_DeviceIsDefault))
			{
				caps.supportedExclusiveSampleRates.push_back(baseSampleRates[n]);
			}
		}
	}
	if(m_HostApiType == paDirectSound)
	{
		if(GetSysInfo().IsOriginal())
		{
			caps.supportedSampleFormats = { SampleFormatFloat32 };
		}
	} else if(m_HostApiType == paMME)
	{
		if(GetSysInfo().IsOriginal())
		{
			caps.supportedSampleFormats = { SampleFormatFloat32 };
		}
	} else if(m_HostApiType == paJACK)
	{
		caps.supportedSampleFormats = { SampleFormatFloat32 };
	} else if(m_HostApiType == paWASAPI)
	{
		caps.supportedSampleFormats = { SampleFormatFloat32 };
	}
#if MPT_OS_WINDOWS
	if(m_HostApiType == paWASAPI && !m_DeviceIsDefault)
	{
		caps.supportedExclusiveModeSampleFormats.clear();
		const std::array<SampleFormat, 5> sampleFormats { SampleFormatInt8, SampleFormatInt16, SampleFormatInt24, SampleFormatInt32, SampleFormatFloat32 };
		for(const SampleFormat sampleFormat : sampleFormats)
		{
			for(const auto sampleRate : caps.supportedExclusiveSampleRates)
			{
				PaStreamParameters StreamParameters;
				MemsetZero(StreamParameters);
				StreamParameters.device = device;
				StreamParameters.channelCount = 2;
				if(sampleFormat.IsFloat())
				{
					StreamParameters.sampleFormat = paFloat32;
				} else
				{
					switch(sampleFormat.GetBitsPerSample())
					{
					case 8: StreamParameters.sampleFormat = paInt8; break;
					case 16: StreamParameters.sampleFormat = paInt16; break;
					case 24: StreamParameters.sampleFormat = paInt24; break;
					case 32: StreamParameters.sampleFormat = paInt32; break;
					}
				}
				StreamParameters.suggestedLatency = 0.0;
				StreamParameters.hostApiSpecificStreamInfo = NULL;
				PaWasapiStreamInfo wasapiStreamInfo;
				MemsetZero(wasapiStreamInfo);
				wasapiStreamInfo.size = sizeof(PaWasapiStreamInfo);
				wasapiStreamInfo.hostApiType = paWASAPI;
				wasapiStreamInfo.version = 1;
				wasapiStreamInfo.flags = paWinWasapiExclusive | paWinWasapiExplicitSampleFormat;
				StreamParameters.hostApiSpecificStreamInfo = &wasapiStreamInfo;
				if(Pa_IsFormatSupported(NULL, &StreamParameters, sampleRate) == paFormatIsSupported)
				{
					caps.supportedExclusiveModeSampleFormats.push_back(sampleFormat);
					break;
				}
			}
		}
	}
	if(m_HostApiType == paWDMKS)
	{
		caps.supportedSampleFormats.clear();
		caps.supportedExclusiveModeSampleFormats.clear();
		const std::array<SampleFormat, 5> sampleFormats { SampleFormatInt8, SampleFormatInt16, SampleFormatInt24, SampleFormatInt32, SampleFormatFloat32 };
		for(const SampleFormat sampleFormat : sampleFormats)
		{
			for(const auto sampleRate : caps.supportedSampleRates)
			{
				PaStreamParameters StreamParameters;
				MemsetZero(StreamParameters);
				StreamParameters.device = device;
				StreamParameters.channelCount = 2;
				if(sampleFormat.IsFloat())
				{
					StreamParameters.sampleFormat = paFloat32;
				} else
				{
					switch(sampleFormat.GetBitsPerSample())
					{
					case 8: StreamParameters.sampleFormat = paInt8; break;
					case 16: StreamParameters.sampleFormat = paInt16; break;
					case 24: StreamParameters.sampleFormat = paInt24; break;
					case 32: StreamParameters.sampleFormat = paInt32; break;
					}
				}
				StreamParameters.suggestedLatency = 0.0;
				StreamParameters.hostApiSpecificStreamInfo = NULL;
				if(Pa_IsFormatSupported(NULL, &StreamParameters, sampleRate) == paFormatIsSupported)
				{
					caps.supportedSampleFormats.push_back(sampleFormat);
					caps.supportedExclusiveModeSampleFormats.push_back(sampleFormat);
					break;
				}
			}
		}
		if(caps.supportedSampleFormats.empty())
		{
			caps.supportedSampleFormats = DefaultSampleFormats<std::vector<SampleFormat>>();
		}
		if(caps.supportedExclusiveModeSampleFormats.empty())
		{
			caps.supportedExclusiveModeSampleFormats = DefaultSampleFormats<std::vector<SampleFormat>>();
		}
	}
#endif // MPT_OS_WINDOWS
#if MPT_OS_WINDOWS
	if((m_HostApiType == paWASAPI) && GetSysInfo().WindowsVersion.IsAtLeast(mpt::Windows::Version::Win7))
	{
		caps.supportedSampleRates = baseSampleRates;
	}
#endif // MPT_OS_WINDOWS

	if(!HasInputChannelsOnSameDevice())
	{
		caps.inputSourceNames.clear();
		auto inputDevices = EnumerateInputOnlyDevices(m_HostApiType);
		for(const auto &dev : inputDevices)
		{
			caps.inputSourceNames.push_back(std::make_pair(static_cast<uint32>(dev.first), dev.second));
		}
	}

	return caps;
}


bool CPortaudioDevice::OpenDriverSettings()
{
#if MPT_OS_WINDOWS
	if(m_HostApiType != paWASAPI)
	{
		return false;
	}
	mpt::PathString controlEXE;
	TCHAR systemDir[MAX_PATH];
	MemsetZero(systemDir);
	if(GetSystemDirectory(systemDir, mpt::saturate_cast<UINT>(std::size(systemDir))) > 0)
	{
		controlEXE += mpt::PathString::FromNative(systemDir);
		controlEXE += P_("\\");
	}
	controlEXE += P_("control.exe");
	return (reinterpret_cast<INT_PTR>(ShellExecute(NULL, TEXT("open"), controlEXE.AsNative().c_str(), TEXT("/name Microsoft.Sound"), NULL, SW_SHOW)) >= 32);
#else // !MPT_OS_WINDOWS
	return false;
#endif // MPT_OS_WINDOWS
}


int CPortaudioDevice::StreamCallback(
	const void *input, void *output,
	unsigned long frameCount,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags
	)
{
	if(!input && !output)
	{
		return paAbort;
	}
	if(m_HostApiType == paWDMKS)
	{
		// For WDM-KS, timeInfo->outputBufferDacTime seems to contain bogus values.
		// Work-around it by using the slightly less accurate per-stream latency estimation.
		m_CurrentRealLatency = m_StreamInfo->outputLatency;
	} else if(m_HostApiType == paWASAPI)
	{
		// PortAudio latency calculation appears to miss the current period or chunk for WASAPI. Compensate it.
		m_CurrentRealLatency = timeInfo->outputBufferDacTime - timeInfo->currentTime + (static_cast<double>(frameCount) / static_cast<double>(m_Settings.Samplerate));
	} else if(m_HostApiType == paDirectSound)
	{
		// PortAudio latency calculation appears to miss the buffering latency.
		// The current chunk, however, appears to be compensated for.
		// Repair the confusion.
		m_CurrentRealLatency = timeInfo->outputBufferDacTime - timeInfo->currentTime + m_StreamInfo->outputLatency - (static_cast<double>(frameCount) / static_cast<double>(m_Settings.Samplerate));
	} else if(m_HostApiType == paALSA)
	{
		// PortAudio latency calculation appears to miss the buffering latency.
		// The current chunk, however, appears to be compensated for.
		// Repair the confusion.
		m_CurrentRealLatency = timeInfo->outputBufferDacTime - timeInfo->currentTime + m_StreamInfo->outputLatency - (static_cast<double>(frameCount) / static_cast<double>(m_Settings.Samplerate));
	} else
	{
		m_CurrentRealLatency = timeInfo->outputBufferDacTime - timeInfo->currentTime;
	}
	m_CurrentFrameBuffer = output;
	m_CurrentFrameBufferInput = input;
	m_CurrentFrameCount = frameCount;
	SourceFillAudioBufferLocked();
	m_CurrentFrameCount = 0;
	m_CurrentFrameBuffer = 0;
	m_CurrentFrameBufferInput = 0;
	if((m_HostApiType == paALSA) && (statusFlags & paOutputUnderflow))
	{
		// PortAudio ALSA does not recover well from buffer underruns
		RequestRestart();
	}
	return paContinue;
}


int CPortaudioDevice::StreamCallbackWrapper(
	const void *input, void *output,
	unsigned long frameCount,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData
	)
{
	return reinterpret_cast<CPortaudioDevice*>(userData)->StreamCallback(input, output, frameCount, timeInfo, statusFlags);
}


std::vector<SoundDevice::Info> CPortaudioDevice::EnumerateDevices(SoundDevice::SysInfo sysInfo)
{
	std::vector<SoundDevice::Info> devices;
	for(PaDeviceIndex dev = 0; dev < Pa_GetDeviceCount(); ++dev)
	{
		if(!Pa_GetDeviceInfo(dev))
		{
			continue;
		}
		if(Pa_GetDeviceInfo(dev)->hostApi < 0)
		{
			continue;
		}
		if(!Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi))
		{
			continue;
		}
		if(!Pa_GetDeviceInfo(dev)->name)
		{
			continue;
		}
		if(!Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->name)
		{
			continue;
		}
		if(Pa_GetDeviceInfo(dev)->maxOutputChannels <= 0)
		{
			continue;
		}
		SoundDevice::Info result = SoundDevice::Info();
		switch((Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->type))
		{
			case paWASAPI:
				result.type = TypePORTAUDIO_WASAPI;
				break;
			case paWDMKS:
				result.type = TypePORTAUDIO_WDMKS;
				break;
			case paMME:
				result.type = TypePORTAUDIO_WMME;
				break;
			case paDirectSound:
				result.type = TypePORTAUDIO_DS;
				break;
			default:
				result.type = U_("PortAudio") + U_("-") + mpt::ufmt::dec(static_cast<int>(Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->type));
				break;
		}
		result.internalID = mpt::ufmt::dec(dev);
		result.name = mpt::ToUnicode(mpt::Charset::UTF8, Pa_GetDeviceInfo(dev)->name);
		result.apiName = mpt::ToUnicode(mpt::Charset::UTF8, Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->name);
		result.extraData[U_("PortAudio-HostAPI-name")] = mpt::ToUnicode(mpt::Charset::UTF8, Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->name);
		result.apiPath.push_back(U_("PortAudio"));
		result.useNameAsIdentifier = true;
		result.flags = {
			Info::Usability::Unknown,
			Info::Level::Unknown,
			Info::Compatible::Unknown,
			Info::Api::Unknown,
			Info::Io::Unknown,
			Info::Mixing::Unknown,
			Info::Implementor::External
		};
		switch(Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->type)
		{
		case paDirectSound:
			result.apiName = U_("DirectSound");
			result.default_ = ((Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->defaultOutputDevice == static_cast<PaDeviceIndex>(dev)) ? Info::Default::Managed : Info::Default::None);
			result.flags = {
				sysInfo.SystemClass == mpt::OS::Class::Windows ? Info::Usability::Deprecated : Info::Usability::NotAvailable,
				Info::Level::Secondary,
				Info::Compatible::No,
				Info::Api::Emulated,
				Info::Io::FullDuplex,
				Info::Mixing::Software,
				Info::Implementor::External
			};
			break;
		case paMME:
			result.apiName = U_("MME");
			result.default_ = ((Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->defaultOutputDevice == static_cast<PaDeviceIndex>(dev)) ? Info::Default::Named : Info::Default::None);
			result.flags = {
				sysInfo.SystemClass == mpt::OS::Class::Windows ? Info::Usability::Legacy : Info::Usability::NotAvailable,
				Info::Level::Secondary,
				Info::Compatible::No,
				Info::Api::Emulated,
				Info::Io::FullDuplex,
				Info::Mixing::Software,
				Info::Implementor::External
			};
			break;
		case paASIO:
			result.apiName = U_("ASIO");
			result.default_ = ((Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->defaultOutputDevice == static_cast<PaDeviceIndex>(dev)) ? Info::Default::Named : Info::Default::None);
			result.flags = {
				sysInfo.SystemClass == mpt::OS::Class::Windows ? sysInfo.IsWindowsOriginal() ? Info::Usability::Usable : Info::Usability::Experimental : Info::Usability::NotAvailable,
				Info::Level::Secondary,
				Info::Compatible::No,
				sysInfo.SystemClass == mpt::OS::Class::Windows && sysInfo.IsWindowsOriginal() ? Info::Api::Native : Info::Api::Emulated,
				Info::Io::FullDuplex,
				Info::Mixing::Hardware,
				Info::Implementor::External
			};
			break;
		case paCoreAudio:
			result.apiName = U_("CoreAudio");
			result.default_ = ((Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->defaultOutputDevice == static_cast<PaDeviceIndex>(dev)) ? Info::Default::Named : Info::Default::None);
			result.flags = {
				sysInfo.SystemClass == mpt::OS::Class::Darwin ? Info::Usability::Usable : Info::Usability::NotAvailable,
				Info::Level::Secondary,
				Info::Compatible::Yes,
				sysInfo.SystemClass == mpt::OS::Class::Darwin ? Info::Api::Native : Info::Api::Emulated,
				Info::Io::FullDuplex,
				Info::Mixing::Server,
				Info::Implementor::External
			};
			break;
		case paOSS:
			result.apiName = U_("OSS");
			result.default_ = ((Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->defaultOutputDevice == static_cast<PaDeviceIndex>(dev)) ? Info::Default::Named : Info::Default::None);
			result.flags = {
				sysInfo.SystemClass == mpt::OS::Class::BSD ? Info::Usability::Usable : sysInfo.SystemClass == mpt::OS::Class::Linux ? Info::Usability::Deprecated : Info::Usability::NotAvailable,
				Info::Level::Primary,
				Info::Compatible::No,
				sysInfo.SystemClass == mpt::OS::Class::BSD ? Info::Api::Native : sysInfo.SystemClass == mpt::OS::Class::Linux ? Info::Api::Emulated : Info::Api::Emulated,
				Info::Io::FullDuplex,
				sysInfo.SystemClass == mpt::OS::Class::BSD ? Info::Mixing::Hardware : sysInfo.SystemClass == mpt::OS::Class::Linux ? Info::Mixing::Software : Info::Mixing::Software,
				Info::Implementor::External
			};
			break;
		case paALSA:
			result.apiName = U_("ALSA");
			result.default_ = ((Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->defaultOutputDevice == static_cast<PaDeviceIndex>(dev)) ? Info::Default::Named : Info::Default::None);
			result.flags = {
				sysInfo.SystemClass == mpt::OS::Class::Linux ? Info::Usability::Usable : Info::Usability::Experimental,
				Info::Level::Primary,
				Info::Compatible::No,
				sysInfo.SystemClass == mpt::OS::Class::Linux ? Info::Api::Native : Info::Api::Emulated,
				Info::Io::FullDuplex,
				sysInfo.SystemClass == mpt::OS::Class::Linux ? Info::Mixing::Hardware : Info::Mixing::Software,
				Info::Implementor::External
			};
			break;
		case paAL:
			result.apiName = U_("OpenAL");
			result.default_ = ((Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->defaultOutputDevice == static_cast<PaDeviceIndex>(dev)) ? Info::Default::Named : Info::Default::None);
			result.flags = {
				Info::Usability::Usable,
				Info::Level::Primary,
				Info::Compatible::No,
				Info::Api::Emulated,
				Info::Io::FullDuplex,
				Info::Mixing::Software,
				Info::Implementor::External
			};
			break;
		case paWDMKS:
			result.apiName = U_("WaveRT");
			result.default_ = ((Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->defaultOutputDevice == static_cast<PaDeviceIndex>(dev)) ? Info::Default::Named : Info::Default::None);
			result.flags = {
				sysInfo.SystemClass == mpt::OS::Class::Windows ? sysInfo.IsWindowsOriginal() ? Info::Usability::Usable : Info::Usability::Broken : Info::Usability::NotAvailable,
				Info::Level::Primary,
				Info::Compatible::No,
				sysInfo.SystemClass == mpt::OS::Class::Windows ? sysInfo.IsWindowsOriginal() ? Info::Api::Native : Info::Api::Emulated : Info::Api::Emulated,
				Info::Io::FullDuplex,
				Info::Mixing::Hardware,
				Info::Implementor::External
			};
			break;
		case paJACK:
			result.apiName = U_("JACK");
			result.default_ = ((Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->defaultOutputDevice == static_cast<PaDeviceIndex>(dev)) ? Info::Default::Managed : Info::Default::None);
			result.flags = {
				sysInfo.SystemClass == mpt::OS::Class::Linux ? Info::Usability::Usable : sysInfo.SystemClass == mpt::OS::Class::Darwin ? Info::Usability::Usable : Info::Usability::Experimental,
				Info::Level::Secondary,
				Info::Compatible::Yes,
				sysInfo.SystemClass == mpt::OS::Class::Linux ? Info::Api::Native : Info::Api::Emulated,
				Info::Io::FullDuplex,
				Info::Mixing::Server,
				Info::Implementor::External
			};
			break;
		case paWASAPI:
			result.apiName = U_("WASAPI");
			result.default_ = ((Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->defaultOutputDevice == static_cast<PaDeviceIndex>(dev)) ? Info::Default::Named : Info::Default::None);
			result.flags = {
				sysInfo.SystemClass == mpt::OS::Class::Windows ? Info::Usability::Usable : Info::Usability::NotAvailable,
				Info::Level::Primary,
				Info::Compatible::No,
				sysInfo.SystemClass == mpt::OS::Class::Windows ? Info::Api::Native : Info::Api::Emulated,
				Info::Io::FullDuplex,
				Info::Mixing::Server,
				Info::Implementor::External
			};
			break;
		default:
			// nothing
			break;
		}
		PALOG(mpt::format(U_("PortAudio: %1, %2, %3, %4"))(result.internalID, result.name, result.apiName, static_cast<int>(result.default_)));
		PALOG(mpt::format(U_(" low  : %1"))(Pa_GetDeviceInfo(dev)->defaultLowOutputLatency));
		PALOG(mpt::format(U_(" high : %1"))(Pa_GetDeviceInfo(dev)->defaultHighOutputLatency));
		if((result.default_ != Info::Default::None) && (Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->type == paWASAPI))
		{
			auto defaultResult = result;
			defaultResult.default_ = Info::Default::Managed;
			defaultResult.name = U_("Default Device");
			defaultResult.internalID = U_("WASAPI-Default");
			defaultResult.useNameAsIdentifier = false;
			devices.push_back(defaultResult);
			result.default_ = Info::Default::Named;
		}
		devices.push_back(result);
	}
	return devices;
}


std::vector<std::pair<PaDeviceIndex, mpt::ustring> > CPortaudioDevice::EnumerateInputOnlyDevices(PaHostApiTypeId hostApiType)
{
	std::vector<std::pair<PaDeviceIndex, mpt::ustring> > result;
	for(PaDeviceIndex dev = 0; dev < Pa_GetDeviceCount(); ++dev)
	{
		if(!Pa_GetDeviceInfo(dev))
		{
			continue;
		}
		if(Pa_GetDeviceInfo(dev)->hostApi < 0)
		{
			continue;
		}
		if(!Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi))
		{
			continue;
		}
		if(!Pa_GetDeviceInfo(dev)->name)
		{
			continue;
		}
		if(!Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->name)
		{
			continue;
		}
		if(Pa_GetDeviceInfo(dev)->maxInputChannels <= 0)
		{
			continue;
		}
		if(Pa_GetDeviceInfo(dev)->maxOutputChannels > 0)
		{ // only find devices with only input channels
			continue;
		}
		if(Pa_GetHostApiInfo(Pa_GetDeviceInfo(dev)->hostApi)->type != hostApiType)
		{
			continue;
		}
		result.push_back(std::make_pair(dev, mpt::ToUnicode(mpt::Charset::UTF8, Pa_GetDeviceInfo(dev)->name)));
	}
	return result;
}


bool CPortaudioDevice::HasInputChannelsOnSameDevice() const
{
	if(m_DeviceIndex == paNoDevice)
	{
		return false;
	}
	const PaDeviceInfo *deviceinfo = Pa_GetDeviceInfo(m_DeviceIndex);
	if(!deviceinfo)
	{
		return false;
	}
	return (deviceinfo->maxInputChannels > 0);
}


#if MPT_COMPILER_MSVC
static void PortaudioLog(const char *text)
{
	if(!text)
	{
		return;
	}
	PALOG(mpt::format(U_("PortAudio: %1"))(mpt::ToUnicode(mpt::Charset::UTF8, text)));
}
#endif // MPT_COMPILER_MSVC


MPT_REGISTERED_COMPONENT(ComponentPortAudio, "PortAudio")


ComponentPortAudio::ComponentPortAudio()
{
	return;
}


bool ComponentPortAudio::DoInitialize()
{
	#if defined(MODPLUG_TRACKER) && MPT_COMPILER_MSVC
		PaUtil_SetDebugPrintFunction(PortaudioLog);
	#endif
	return (Pa_Initialize() == paNoError);
}


ComponentPortAudio::~ComponentPortAudio()
{
	if(IsAvailable())
	{
		Pa_Terminate();
	}
}


#endif // MPT_WITH_PORTAUDIO


} // namespace SoundDevice


OPENMPT_NAMESPACE_END