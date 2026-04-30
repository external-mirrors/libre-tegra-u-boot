.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: bctupdate (command)

bctupdate command
=================

Synopsis
--------

::

    bctupdate [<bct_addr>] [<bct_size>]

Description
-----------

The "bctupdate" command is used to self-update bootloader on Tegra 2 and Tegra 3
production devices.

The "bctupdate" performs encryption of new bootloader, loads existing BCT to
decrypt, patch and re-encrypt. After BCT and bootloader are written in
their respective places in the boot device.

bct
    address of the plaintext BCT pre-loaded into RAM.

size
    size of the pre-loaded bootloader.

Example
-------

::

	load mmc 1:1 ${kernel_addr_r} ${bootloader_file};
	size mmc 1:1 ${bootloader_file};
	bctupdate ${kernel_addr_r} ${filesize};

Configuration
-------------

The bctupdate command is only available if CONFIG_CMD_EBTUPDATE=y and
only on supported Tegra configurations.
