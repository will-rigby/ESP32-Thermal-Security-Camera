# Thermal image contrast

The **Adaptive detail** image mode allocates more of the available grayscale and palette range to temperature bands represented in the scene. This can make objects with similar temperatures easier to distinguish when another object is much hotter or colder. **Linear** remains available for comparison. Adaptive detail is the default; the saved configuration field is `tone_mapping`, with values `adaptive` and `linear`.

The mapping is applied to the grayscale video before browser palettes are added. USB video, browser video and BMP snapshots therefore use the same contrast adjustment. Palettes continue to change colour presentation independently.

## Why use a histogram instead of a logarithmic scale?

Linear mapping assigns equal display intervals to equal temperature intervals. A very hot object can consequently leave only a small part of the display range for cooler objects that differ by a few degrees. FLIR describes this limitation and uses histogram-based presentation to allocate display levels to temperatures actually present in an image. [FLIR: colour presentation and histogram equalization](https://www.flir.com/en-asia/support-center/instruments2/what-happens-when-i-select-histogram-equalization-as-the-image-presentation-in-flir-tools/).

A logarithmic curve can help a scene whose interesting details are concentrated at its cooler end, but it also compresses differences at the hotter end. For example, applying `log(1 + k*x) / log(1 + k)` to a normalized temperature `x` between zero and one expands low values when `k` is positive. It does not inspect the scene to find useful temperature groups. Applying a logarithm directly to Celsius is unsuitable because Celsius values can be zero or negative and the arbitrary temperature zero would affect the result.

A histogram-based curve adapts to the temperature distribution. Conventional histogram equalization can overemphasize a large, nearly uniform background. FLIR's plateau approach limits populated histogram bins and provides controls for a linear contribution, maximum gain and temporal damping. Its documentation also explains the tradeoff between faster adaptation and background flashing. [FLIR: Camera Adjustments application note, sections 2.0 and 2.2](https://flir.netx.net/file/asset/15755/original/attachment).

## Implementation choices

These constants are project tuning choices informed by those techniques, not FLIR-prescribed settings or a reproduction of a FLIR algorithm:

- Build a **128-bin global histogram** covering all valid scene temperatures, allowing the minimum span and smoothed range boundaries described below.
- Limit histogram bins to **four times the mean bin population** and redistribute the clipped population when building the contrast-limited mapping.
- At full strength, blend **65% histogram mapping with 35% linear mapping**. The linear contribution retains some separation across temperature gaps that an unmodified cumulative histogram would compress.
- Use a **2°C minimum display span**. Below a measured scene span of 2°C, fade the histogram contribution quadratically toward zero to restrain noise amplification.
- Smooth changes with elapsed-time-based damping using a **400 ms time constant**. Expand the range immediately for new extremes and contract it gradually when extremes leave. Reproject the previous mapping into the current temperature coordinates before blending it, so a moving range does not mix unrelated temperatures. Initialize a fresh mapping after mode changes, disjoint scene ranges or gaps of at least one second. This also accommodates the adjustable image refresh rate.
- Apply one monotonic mapping across the entire image. Pixels at the same temperature receive the same display level before any selected Black-hot inversion.
- Keep **Linear** as a saved alternative for examining the scene without histogram enhancement.

The histogram and lookup table are small. The mapper scans the sensor pixels and uses bounded passes over the histogram to redistribute clipped counts without exceeding the cap. Mapping occurs when generating an image; detection continues on its existing sensor schedule.

### Why there is no percentile trimming

This firmware uses all valid pixels when choosing the scene range. It does not drop a fixed percentage of the hottest or coldest pixels. The sensor has 4,960 pixels: a 1% cut would discard roughly 50 pixels at each end, while the detector can be configured for objects much smaller than that. Such clipping can flatten the visible temperature differences within a small animal or other target.

Using the full range retains those extremes. The histogram adjustment instead makes better use of display levels inside that range. This is a deliberate tradeoff: an extreme value can still influence the range. FLIR also documents that tail rejection can improve central contrast while washing out small extreme-temperature objects. [FLIR: Camera Adjustments, Tail Rejection](https://flir.netx.net/file/asset/15755/original/attachment).

### Why there is no local CLAHE

Contrast-limited adaptive histogram equalization (CLAHE) computes separate histograms for image tiles. OpenCV documents its contrast limit as a way to control noise amplification and its interpolation as a way to reduce tile-boundary artifacts. [OpenCV: histogram equalization and CLAHE](https://docs.opencv.org/4.8.0/d5/daf/tutorial_py_histogram_equalization.html).

CLAHE can reveal local detail, but the same temperature can then receive different brightness values at different locations. For this 80 × 62 camera, the project chooses a global curve to retain one temperature-to-display relationship throughout a frame and avoid adding tile-dependent behaviour. This is an implementation judgment, not a claim that CLAHE is generally inferior.

## Temperature readings and detection

Adaptive detail changes the displayed image. It does not change calibrated sensor values, temperature thresholds, background learning, occupancy decisions or the reported scene minimum and maximum temperatures. Those values continue to come from the native Celsius data.

Video and snapshots are presentation images, not radiometric temperature records. Their grayscale or palette values must not be converted back to Celsius by assuming a linear scale. Automatic mapping can also change an object's displayed colour when other scene content changes. Use the temperature measurements for numeric comparisons.

Improved display contrast does not add sensor pixels or establish a better detection range. The manufacturer's published 125 mK noise-equivalent temperature difference is qualified in the MI0802 datasheet by a 1 FPS measurement with temporal denoising; it is not proof of this firmware's noise performance at 20 FPS. [Waveshare module documentation](https://www.waveshare.com/wiki/Thermal-Camera-ESP32-Module), [MI0802 datasheet, thermal imaging characteristics](https://files.waveshare.com/wiki/Thermal-Camera-HAT/MI0802--Datasheet--v1.0.1.pdf).

## Validation status

Firmware 0.4.0 was built, flashed and verified on 2026-10-17. Native tests passed for close-temperature objects with a hotter outlier, tiny warm targets, uniform/noisy scenes, negative/nonfinite values, monotonicity, Black-hot inversion, scene transitions and timestamp handling. One controlled synthetic scene achieved 2.95 times the grayscale separation of Linear; this is scene-specific and does not establish improved detection range.

Live mode switching, invalid-value rejection, snapshots, configuration preservation and reboot persistence passed. Concurrent Adaptive streams measured about 18.2 FPS in the browser and 18.9 FPS over USB, with no sensor or allocation failures. The user confirmed a clear hand shape at approximately 20-30 cm, establishing a meaningful thermal image. A 100-frame diagnostic capture showed predominantly frame-to-frame noise upstream of colour mapping; representative comparisons of object visibility and noise remain necessary. See the [dated hardware validation record](HARDWARE_VALIDATION.md) for measurements and limitations.
