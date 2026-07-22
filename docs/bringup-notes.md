setup jetson / rpi to connect to wifi & hotspot 

set them up on tailscale 

```
ssh jetson
ssh rpi
```

Motor mapping & calibration
```
d0 (back right) range 1485-1520 
d1 (front left) range 1505- 1540
d2 (back left) range 1505-1540 
d3 (front right) range 1525-1560
where range is defined as not moving 
```

Current calibration magnitudes:
  forward:  FL=80 FR=40 BL=80 BR=60
  backward: FL=40 FR=80 BL=40 BR=75

Codex identified an issue with the movement:
https://resources.finalsite.net/images/v1757346007/a2schoolsorg/zx7nkn8dgshpva53zgfy/MecanumDrive_PrinciplesandProgramming.pdf

motors were borked, had to install them different ways 