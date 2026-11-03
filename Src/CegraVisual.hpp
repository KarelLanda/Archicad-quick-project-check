#pragma once

#include "ACAPinc.h"
#include "HashSet.hpp"

/* Dočasné zvýraznění v 2D/3D: API jako Property Highlighter (per-prvek + barva ostatních). */
void CegraVisual_ApplyHighlight (const GS::Array<API_Guid>& resultGuids, const API_RGBAColor& hitColor, const API_RGBAColor& restColor);

/* Ztlumí ostatní prvky v pohledu (restColor), zvýrazní případné hitGuids (hitColor),
 * ale prvky v excludedGuids ponechá bez barevné masky (aby vyniklo přebarvené pero konkrétního textu kóty). */
void CegraVisual_ApplyHighlightWithExcluded (
	const GS::Array<API_Guid>&		hitGuids,
	const GS::HashSet<API_Guid>&	excludedGuids,
	const API_RGBAColor&			hitColor,
	const API_RGBAColor&			restColor);

void CegraVisual_Remove (void);
