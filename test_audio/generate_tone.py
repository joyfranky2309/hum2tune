
import math
import wave
import struct

sample_rate = 44100
frequency = 440.0
duration = 2.0
amplitude = 12000

with wave.open("test_audio/tone.wav", "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(sample_rate)

    for i in range(int(sample_rate * duration)):
        sample = int(
            amplitude * math.sin(
                2 * math.pi * frequency * i / sample_rate
            )
        )
        wav.writeframesraw(struct.pack("<h", sample))

print("Generated test_audio/tone.wav")
