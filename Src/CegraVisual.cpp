#include "CegraVisual.hpp"
#include "ACAPI_Automate.h"
#include "ACAPI_Interface.h"
#include "HashTable.hpp"

void CegraVisual_ApplyHighlight (const GS::Array<API_Guid>& resultGuids, const API_RGBAColor& hitColor, const API_RGBAColor& restColor)
{
	ACAPI_UserInput_ClearElementHighlight ();
	if (resultGuids.IsEmpty ()) {
		(void) ACAPI_View_Redraw ();
		return;
	}
	GS::HashTable<API_Guid, API_RGBAColor> table;
	for (UIndex i = 0; i < resultGuids.GetSize (); ++i) {
		table.Put (resultGuids[i], hitColor);
	}
	ACAPI_UserInput_SetElementHighlight (table, GS::NoValue, restColor);
	(void) ACAPI_View_Redraw ();
}

void CegraVisual_ApplyHighlightWithExcluded (
	const GS::Array<API_Guid>&		hitGuids,
	const GS::HashSet<API_Guid>&	excludedGuids,
	const API_RGBAColor&			hitColor,
	const API_RGBAColor&			restColor)
{
	ACAPI_UserInput_ClearElementHighlight ();
	if (hitGuids.IsEmpty () && excludedGuids.IsEmpty ()) {
		(void) ACAPI_View_Redraw ();
		return;
	}
	if (excludedGuids.IsEmpty ()) {
		CegraVisual_ApplyHighlight (hitGuids, hitColor, restColor);
		return;
	}

	GS::HashSet<API_Guid> hitSet;
	for (UIndex i = 0; i < hitGuids.GetSize (); ++i) {
		if (!excludedGuids.Contains (hitGuids[i])) {
			hitSet.Add (hitGuids[i]);
		}
	}

	GS::HashTable<API_Guid, API_RGBAColor> table;
	for (auto it = hitSet.Enumerate (); it != nullptr; ++it) {
		table.Put (*it, hitColor);
	}

	GS::Array<API_Guid> allElems;
	if (ACAPI_Element_GetElemList (API_ZombieElemID, &allElems) == NoError) {
		for (const API_Guid& g : allElems) {
			if (g == APINULLGuid || excludedGuids.Contains (g) || hitSet.Contains (g)) {
				continue;
			}
			table.Put (g, restColor);
		}
	}

	if (!table.IsEmpty ()) {
		ACAPI_UserInput_SetElementHighlight (table, GS::NoValue, GS::NoValue);
	}
	(void) ACAPI_View_Redraw ();
}

void CegraVisual_Remove (void)
{
	ACAPI_UserInput_ClearElementHighlight ();
	(void) ACAPI_View_Redraw ();
}
