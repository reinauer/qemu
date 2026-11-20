/*
 * Freescale eTSEC 2.0 MDIO controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NET_FSL_ETSEC_MDIO_H
#define HW_NET_FSL_ETSEC_MDIO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_FSL_ETSEC_MDIO "fsl-etsec2-mdio"
OBJECT_DECLARE_SIMPLE_TYPE(FslEtsecMdioState, FSL_ETSEC_MDIO)

struct eTSEC;

struct FslEtsecMdioState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* The eTSEC whose PHY answers at phy_addr on this bus */
    struct eTSEC *etsec;
    uint8_t phy_addr;

    uint32_t miimcfg;
    uint32_t miimcom;
    uint32_t miimadd;
    uint32_t miimcon;
    uint32_t miimstat;
};

#endif
