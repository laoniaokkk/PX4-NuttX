/****************************************************************************
 * boards/arm/at32/at32f429ig-yt/src/at32_w25.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <debug.h>

#ifdef CONFIG_AT32_SPI1
#include <nuttx/spi/spi.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/fs/smart.h>
#include <nuttx/mtd/configdata.h>
#endif

#include "at32_spi.h"

#include "at32f437-mini.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Debug ********************************************************************/

/* Non-standard debug that may be enabled just for testing the watchdog
 * timer
 */

#define W25_SPI_PORT 1

/* Configuration ************************************************************/

/* Can't support the W25 device if it SPI2 or W25 support is not enabled */

#define HAVE_W25 1
#if !defined(CONFIG_AT32_SPI1) || !defined(CONFIG_MTD_W25)
#undef HAVE_W25
#endif

/* Can't support W25 features if mountpoints are disabled */

#if defined(CONFIG_DISABLE_MOUNTPOINT)
#undef HAVE_W25
#endif

/* Can't support both FAT and SMARTFS */

#if defined(CONFIG_FS_FAT) && defined(CONFIG_FS_SMARTFS)
#warning "Can't support both FAT and SMARTFS -- using FAT"
#endif

#if defined(CONFIG_MTD_CONFIG)
#define FLASH_CONFIG_PART_NUMBER    0 /* Config part0 as /dev/config */
#endif

#define HAVE_FLASH_PART     1 /* The flash part is more than one */

#define W25QXX_FLASH_MINOR  0

#define PART_LIST "512, 4096, 1024" /* Part size(KB) */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: at32_w25initialize
 *
 * Description:
 *   Initialize and register the W25 FLASH file system.
 *
 ****************************************************************************/

int at32_w25initialize(int minor)
{
  int ret;

#ifdef HAVE_W25

  struct spi_dev_s *spi;
  struct mtd_dev_s *mtd;

  /* Get the SPI port */

  spi = at32_spibus_initialize(W25_SPI_PORT);
  if (!spi)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize SPI port %d\n",
            W25_SPI_PORT);
      return -ENODEV;
    }

  /* Now bind the SPI interface to the W25 SPI FLASH driver */

  mtd = w25_initialize(spi);
  if (!mtd)
    {
      syslog(LOG_ERR, "ERROR: Failed to bind SPI port %d to the Winbond"
                      "W25 FLASH driver\n",
            W25_SPI_PORT);
      return -ENODEV;
    }

  /* Use fat filesystem */

#ifdef CONFIG_FS_FAT
  ret = ftl_initialize(minor, mtd);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Initialize the FTL layer\n");
      return ret;
    }

  /* Use smart filesystem */

#else

  /* There is some parts */

#ifdef HAVE_FLASH_PART 
{
    int partno;
    int partsize;
    int partoffset;
    int partszbytes;
    int erasesize;
    int blocksize;
    const char *partstring = PART_LIST;
    const char *ptr;
    struct mtd_dev_s *mtd_part;
    char partref[16];
    struct mtd_geometry_s geo;

  /* Initialize to provide SMARTFS on the MTD interface */

  /* Get the geometry of the FLASH device */

    ret = mtd->ioctl(mtd, MTDIOC_GEOMETRY, (unsigned long)((uintptr_t)&geo));
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: mtd->ioctl failed: %d\n", ret);
      return ret;
    }

  /* Now create a partition on the FLASH device */

    partno = 0;
    ptr = partstring;
    partoffset = 64;   /* Start block */

  /* Get the Flash erase size */

    erasesize = geo.erasesize;
    blocksize = geo.blocksize;

  while (*ptr != '\0')
    {
      /* Get the partition size */

        partsize = atoi(ptr);
        partszbytes = (partsize << 10); /* partsize is defined in KB */

      /* Check if partition size is bigger then erase block */

      if (partszbytes < erasesize)
        {
          syslog(LOG_ERR,
                "ERROR: Partition size is lesser than erasesize!\n");
          return -1;
        }

      /* Check if partition size is multiple of erase block */

      if ((partszbytes % erasesize) != 0)
        {
          syslog(LOG_ERR,
                "ERROR: Partition size isn't multiple of erasesize!\n");
          return -1;
        }

        mtd_part = mtd_partition(mtd, partoffset, partszbytes / blocksize);
        partoffset += partszbytes / blocksize;

  #if defined(CONFIG_MTD_CONFIG)
      /* Test if this is the config partition */

      if (FLASH_CONFIG_PART_NUMBER == partno)
        {
          /* Register the partition as the config device */

          mtdconfig_register(mtd_part);
        }
        else
  #endif
        {
      /* Now initialize a SMART Flash block device and bind it
       * to the MTD device.
       */

  #if defined(CONFIG_MTD_SMART) && defined(CONFIG_FS_SMARTFS)
          snprintf(partref, sizeof(partref), "p%d", partno);
          smart_initialize(W25QXX_FLASH_MINOR,
                          mtd_part, partref);
  #endif
        }

      /* Update the pointer to point to the next size in the list */

      while ((*ptr >= '0') && (*ptr <= '9'))
        {
          ptr++;
        }

      while (*ptr == ','|| *ptr==' ')
        {
          ptr++;
        }

      /* Increment the part number */

        partno++;
    }
    }
#else /* CONFIG_FLASH_PART */

  /* Configure the device with no partition support */

  smart_initialize(W25QXX_FLASH_MINOR, mtd, NULL);

#endif /* HAVE_FLASH_PART */
#endif /* CONFIG_FS_FAT */
#endif /* HAVE_W25 */

  return OK;
}
