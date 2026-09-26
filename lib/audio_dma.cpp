#include <aurora/audio_dma.hpp>
#include <aurora/exception.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace aurora::audio {
struct DmaAudioOutput::Impl {
  SDL_AudioStream* stream = nullptr;
  std::uint64_t frames = 0;
  std::uint64_t nonzero = 0;
  ~Impl() {
    if (stream) SDL_DestroyAudioStream(stream);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
};
DmaAudioOutput::DmaAudioOutput(int sample_rate) : m_impl(std::make_unique<Impl>()) {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    aurora::throw_host_exception<std::runtime_error>(SDL_GetError());
  const std::string driver = SDL_GetCurrentAudioDriver();
  if (driver == "dummy" || driver == "disk")
    aurora::throw_host_exception<std::runtime_error>("Audio DMA requires an audible playback device");
  const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, sample_rate};
  m_impl->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!m_impl->stream || !SDL_ResumeAudioStreamDevice(m_impl->stream))
    aurora::throw_host_exception<std::runtime_error>(SDL_GetError());
}
DmaAudioOutput::~DmaAudioOutput() = default;
void DmaAudioOutput::submit(std::span<const std::int16_t> stereo) {
  if (stereo.size() % 2 || !SDL_PutAudioStreamData(m_impl->stream, stereo.data(), static_cast<int>(stereo.size_bytes())))
    aurora::throw_host_exception<std::runtime_error>("Audio DMA submission failed: " + std::string(SDL_GetError()));
  m_impl->frames += stereo.size() / 2;
  m_impl->nonzero += std::ranges::count_if(stereo, [](auto sample) { return sample != 0; });
}
std::uint64_t DmaAudioOutput::submitted_frames() const { return m_impl->frames; }
std::uint64_t DmaAudioOutput::nonzero_samples() const { return m_impl->nonzero; }
}
