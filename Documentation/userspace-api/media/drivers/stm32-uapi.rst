.. SPDX-License-Identifier: GPL-2.0

STM32 Chrom-Art 2D Graphics Accelerator unit (DMA2D) driver
===========================================================

The DMA2D driver implements the following driver-specific controls:

``V4L2_CID_DMA2D_R2M_MODE (boolean)``
-------------------------------------
    Enable/Disable the Register-To-Memory mode, filling a part or the
    whole of a destination image with a specific color.

    1 for enable, 0 for disable.
