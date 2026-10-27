# Avatar assets

The first avatar set uses the cat and dog pixel sprites by **bonzille**, published on OpenGameArt under **CC0 1.0 Universal**:

- Original listing and license: https://opengameart.org/content/pixel-art-dog-and-cat
- Cat PNG: https://opengameart.org/sites/default/files/pixel_cat_0.png
- Dog PNG: https://opengameart.org/sites/default/files/pixel_dog_0.png

The source PNGs are retained here. `generate_avatars.py` crops their white matte, scales them with nearest-neighbor sampling, and emits compact RGB565 plus alpha LVGL images in `cyd_avatars.c`. Attribution is not required by CC0; this notice is kept for provenance.

Run from `firmware/main/assets/`:

```powershell
python generate_avatars.py
```
