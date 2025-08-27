from __future__ import print_function
import scipy.io.wavfile as wavf
import numpy as np
import wave

if __name__ == "__main__":

    samples = []

    with open('samples.txt', 'r') as f:
        for line in f:
            samples.append([float(x) for x in line.split()])

    print("\nSAMPLES NP\n")
    samples_np = np.array(samples).flatten()

    samples_np = np.subtract(samples_np, 2048)

    samples_np = np.divide(samples_np, 2048)

    print(samples_np)

    fs = 44100
    out_f = 'out.wav'

    wavf.write(out_f, fs, samples_np)
