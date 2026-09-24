# CarefulSource

## Usage

```
cs.ImageSource(string path[, int yuv_to_rgb=False])
```

- path: Path to image file
- yuv_to_rgb: RGB output using internal JPEG/AVIF upsampling for chroma

```
cs.ConvertColor(vnode clip, string output_profile[, string input_profile, int float_output=False])
```

- clip: Clip to process
- output_profile: Path to ICC profile to transform to - Predefined profiles ["srgb", "srgb-gray", "xyz"]
- input_profile: Profile to transform from
- float_output: Output as float

## Formats

- [x] AVIF
- [x] HEIC
- [x] JPEG
- [x] JXL
- [x] PNG
- [x] TIFF
- [x] WEBP
