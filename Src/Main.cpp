#include "APIEnvir.h"
#include "ACAPinc.h"
#include "ACAPI_Automate.h"
#include "ACAPI_Interface.h"
#include "ACAPI/Favorite.hpp"
#include "ACAPI/ElementDefault.hpp"
#include "APIdefs_Attributes.h"
#include "AttributeCheckers.hpp"
#include "ElementCheckers.hpp"
#include "CegraVisual.hpp"
#include "Resources.hpp"
#include "ToolIconsResIDs.hpp"
#include "CH.hpp"
#include "DG.h"
#include "DGDefs.h"
#include "DGCommandDescriptor.hpp"
#include "DGCommandEvent.hpp"
#include "DGItemEventArgs.hpp"
#include "DGMenu.hpp"
#include "DGUtility.hpp"
#include "HashTable.hpp"
#include "HashSet.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

namespace {

constexpr short	MinPaletteWidth  = 920;
constexpr short	MinPaletteHeight = 560;
constexpr ULong	kCegraDev        = 944205126u;
constexpr ULong	kCegraProduct    = 2244564690u;
constexpr double kPi             = 3.14159265358979323846;

enum class CheckMode { Hairy, Modular };

enum class CategoryId : short {
	Accuracy	= 0,
	Attributes	= 1,
	Properties	= 2,
	Elements	= 3,
	Defaults	= 4,
	Favorites	= 5,
	Views		= 6,
	Docs		= 7
};

struct TreeChildNode {
	short			subIdx			= 0;
	Int32			tvItem			= DG_TVI_NONE;
	GS::UniString	baseName;
	bool			checked			= true;
	bool			implemented		= false;
	bool			hasRun			= false;
	int				issueCount		= 0;
};

struct TreeCategoryNode {
	CategoryId					id			= CategoryId::Accuracy;
	Int32						tvItem		= DG_TVI_NONE;
	GS::UniString				baseName;
	GS::Array<TreeChildNode>	children;
};

struct ScanResult {
	API_Guid		guid				= APINULLGuid;
	API_ElemTypeID	type				= API_ZombieElemID;
	GS::UniString	checkedParam;
	double			lengthMm			= 0.0;
	double			lengthDeviationMm	= 0.0;
	double			angleDeg			= 0.0;
	double			angleDeviationDeg	= 0.0;
	bool			hasAngleValue		= true;
	bool			is3DCapable			= false;
	GS::UniString	homeStory;
	GS::UniString	layerName;
	bool			layerHidden			= false;
};

struct PaletteState {
	short						dialogId				= 0;
	CategoryId					selectedCategory		= CategoryId::Accuracy;
	short						selectedSubIdx			= 0; /* -1 = vybrána nadřazená kategorie, >= 0 = konkrétní pod-kontrola */
	bool						accuracyHasRun			= false;
	GS::Array<ScanResult>		results;
	GS::Array<SubCheckState>	attrSubChecks;
	GS::Array<SubCheckState>	propSubChecks;
	GS::Array<SubCheckState>	elemSubChecks;
	GS::Array<SubCheckState>	defSubChecks;
	GS::Array<SubCheckState>	favSubChecks;
	GS::Array<SubCheckState>	viewSubChecks;
	GS::Array<SubCheckState>	docSubChecks;
	GS::Array<TreeCategoryNode>	treeCategories;
	GS::HashSet<API_Guid>		disabledClassSystems;
};

PaletteState		gPaletteState;

/* Zvýraznění řádku odpovídajícího prvnímu vybranému prvku v seznamu výsledků. */
static API_Guid		gChlupListHighlightGuid	= APINULLGuid;

static void RefreshResultList (short dialogId);
static void UpdateRightPanelLayout (short dialogId);
static void UpdateTreeNodesDisplay (short dialogId);
static void RecolorModelByCurrentTab (short dialogId, bool manualButtonTrigger);

static API_RGBAColor	gHitRgba				= {};
static API_RGBAColor	gRestRgba				= {};
static Int32			gListDecimals			= 4;
static bool				gEnableHighlight		= true;
static short			gDimHighlightPen		= 20;
static double			gStaticDimCircleDiamMm	= 200.0;

/* Dočasné hodnoty v otevřeném modálním dialogu Nastavení. */
static API_RGBAColor						sTempHitRgba				= {};
static API_RGBAColor						sTempRestRgba				= {};
static short								sTempDimHighlightPen		= 20;
static GS::HashSet<API_Guid>				sTempDisabledClassSystems;
static GS::Array<API_ClassificationSystem>	sTempProjectClassSystems;

/* Mezipaměť původních per kótovacích textů (notePen), dočasných kružnic statických kót
 * a stav vybraného řádku v kontrole kót. */
struct DimNotePenTarget {
	API_Guid	elemGuid		= APINULLGuid;
	Int32		subElemIndex	= -1;
};

struct DimNotePenBackupEntry {
	API_Guid	elemGuid			= APINULLGuid;
	Int32		subElemIndex		= -1;
	short		originalNotePen		= 1;
	short		appliedHighlightPen	= 20;
};

struct StaticDimCircleTarget {
	API_Guid	parentDimGuid	= APINULLGuid;
	Int32		subElemIndex	= -1;
	API_Coord	centerPos		= { 0.0, 0.0 };
};

struct StaticDimCircleEntry {
	API_Guid		circleGuid					= APINULLGuid;
	API_Guid		parentDimGuid				= APINULLGuid;
	Int32			subElemIndex				= -1;
	API_Coord		centerPos					= { 0.0, 0.0 };
	short			appliedPen					= 20;
	double			appliedDiamMm				= 200.0;
#if defined (ServerMainVers_3000)
	bool			usedNativeWitnessPen		= false;
	bool			origWitnessOverride			= false;
	short			origWitnessExtPen			= 1;
	API_WitnessID	origWitnessForm				= APIWtn_Fix;
#endif
};

static GS::Array<DimNotePenBackupEntry>		gDimPenBackup;
static GS::Array<StaticDimCircleEntry>		gStaticDimCircles;
static Int32								gSelectedCustomDimRowIndex	= -1;
static bool									gIsSyncingDimPens			= false;

static void RestoreDimensionNotePenOverrides (void);
static void SyncDimensionNotePenOverrides (const GS::Array<DimNotePenTarget>& targets);
static void RestoreStaticDimensionCircles (void);
static void SyncStaticDimensionCircles (const GS::Array<StaticDimCircleTarget>& targets);
static void RestoreAllDimensionHighlights (void);

static constexpr UInt32 kChlupPrefsMagic	= 0x434C5032u; /* CLP2 */
static constexpr Int32	kChlupPrefsVersion	= 4;

#pragma pack(push, 1)
struct ChlupPrefsBlobV3 {
	UInt32	magic;
	Int32	version;
	float	hitR, hitG, hitB, hitA;
	float	restR, restG, restB, restA;
	Int32	listDecimals;
	UInt8	enableHighlight;
	UInt8	pad;
	Int16	dimHighlightPen;
};

struct ChlupPrefsBlob {
	UInt32	magic;
	Int32	version;
	float	hitR, hitG, hitB, hitA;
	float	restR, restG, restB, restA;
	Int32	listDecimals;
	UInt8	enableHighlight;
	UInt8	pad;
	Int16	dimHighlightPen;
	double	staticDimCircleDiamMm;
};
#pragma pack(pop)

static void DefaultChlupPreferences (void)
{
	/* Výchozí barva zvýraznění: pastelová červená */
	gHitRgba.f_red			= 0.95;
	gHitRgba.f_green		= 0.42;
	gHitRgba.f_blue			= 0.42;
	gHitRgba.f_alpha		= 1.00;
	gRestRgba.f_red			= 0.55;
	gRestRgba.f_green		= 0.55;
	gRestRgba.f_blue		= 0.55;
	gRestRgba.f_alpha		= 0.45;
	gListDecimals			= 4;
	gEnableHighlight		= true;
	gDimHighlightPen		= 20;
	gStaticDimCircleDiamMm	= 200.0;
}

static void LoadChlupPreferences (void)
{
	DefaultChlupPreferences ();
	Int32	ver		= 0;
	GSSize	nBytes	= 0;
	ACAPI_GetPreferences (&ver, &nBytes, nullptr);
	if (nBytes == sizeof (ChlupPrefsBlob)) {
		ChlupPrefsBlob b = {};
		ACAPI_GetPreferences (&ver, &nBytes, &b);
		if (b.magic != kChlupPrefsMagic || b.version != kChlupPrefsVersion) {
			return;
		}
		gHitRgba.f_red		= b.hitR;
		gHitRgba.f_green	= b.hitG;
		gHitRgba.f_blue		= b.hitB;
		gHitRgba.f_alpha	= b.hitA;
		gRestRgba.f_red		= b.restR;
		gRestRgba.f_green	= b.restG;
		gRestRgba.f_blue	= b.restB;
		gRestRgba.f_alpha	= b.restA;
		gListDecimals		= std::clamp<Int32> (b.listDecimals, 0, 10);
		gEnableHighlight	= (b.enableHighlight != 0);
		gDimHighlightPen	= (b.dimHighlightPen >= 1 && b.dimHighlightPen <= 255)
			? static_cast<short> (b.dimHighlightPen)
			: 20;
		gStaticDimCircleDiamMm = (b.staticDimCircleDiamMm >= 1.0 && b.staticDimCircleDiamMm <= 50000.0)
			? b.staticDimCircleDiamMm
			: 200.0;
	} else if (nBytes == sizeof (ChlupPrefsBlobV3)) {
		ChlupPrefsBlobV3 b = {};
		ACAPI_GetPreferences (&ver, &nBytes, &b);
		if (b.magic != kChlupPrefsMagic || b.version != 3) {
			return;
		}
		gHitRgba.f_red		= b.hitR;
		gHitRgba.f_green	= b.hitG;
		gHitRgba.f_blue		= b.hitB;
		gHitRgba.f_alpha	= b.hitA;
		gRestRgba.f_red		= b.restR;
		gRestRgba.f_green	= b.restG;
		gRestRgba.f_blue	= b.restB;
		gRestRgba.f_alpha	= b.restA;
		gListDecimals		= std::clamp<Int32> (b.listDecimals, 0, 10);
		gEnableHighlight	= (b.enableHighlight != 0);
		gDimHighlightPen	= (b.dimHighlightPen >= 1 && b.dimHighlightPen <= 255)
			? static_cast<short> (b.dimHighlightPen)
			: 20;
	}
}

static void SaveChlupPreferences (void)
{
	ChlupPrefsBlob b = {};
	b.magic					= kChlupPrefsMagic;
	b.version				= kChlupPrefsVersion;
	b.hitR					= static_cast<float> (gHitRgba.f_red);
	b.hitG					= static_cast<float> (gHitRgba.f_green);
	b.hitB					= static_cast<float> (gHitRgba.f_blue);
	b.hitA					= static_cast<float> (gHitRgba.f_alpha);
	b.restR					= static_cast<float> (gRestRgba.f_red);
	b.restG					= static_cast<float> (gRestRgba.f_green);
	b.restB					= static_cast<float> (gRestRgba.f_blue);
	b.restA					= static_cast<float> (gRestRgba.f_alpha);
	b.listDecimals			= std::clamp<Int32> (gListDecimals, 0, 10);
	b.enableHighlight		= gEnableHighlight ? 1 : 0;
	b.dimHighlightPen		= static_cast<Int16> (std::clamp<short> (gDimHighlightPen, 1, 255));
	b.staticDimCircleDiamMm	= std::clamp (gStaticDimCircleDiamMm, 1.0, 50000.0);
	(void) ACAPI_SetPreferences (kChlupPrefsVersion, sizeof (b), (const void*) &b);
}

static void UpdateLiveSyncControlsState (short dialogId)
{
	if (dialogId == 0) {
		return;
	}
	const bool liveSync = (DGGetItemValLong (dialogId, ChkLiveSync) != 0);
	if (liveSync) {
		DGDisableItem (dialogId, BtnRecolorByTab);
	} else {
		DGEnableItem (dialogId, BtnRecolorByTab);
	}
}

static void SetPaletteItemsEnabled (short dialogId, bool enabled)
{
	if (dialogId == 0) {
		return;
	}
	const short items[] = {
		BtnWall, BtnBeam, BtnColumn, BtnLine, BtnPolyLine,
		ChkSelectedOnly, BtnSettings,
		RadActiveView, RadWholeModel, RadHairy, RadModular,
		EditModuleMm, EditToleranceMm, EditAngleTolerance,
		ResultList, TxtStatus, BtnReset, BtnCheck, BtnCheckSelected,
		BtnRecolorByTab, ChkLiveSync,
		BtnInfoTolLength, BtnInfoTolAngle,
		BtnTreeCheckAll, BtnTreeUncheckAll, TreeChecks
	};
	for (short item : items) {
		if (enabled) {
			DGEnableItem (dialogId, item);
		} else {
			DGDisableItem (dialogId, item);
		}
	}
	if (enabled) {
		UpdateLiveSyncControlsState (dialogId);
	}
}

static void SetStatusText (short dialogId, const GS::UniString& text)
{
	DGSetItemText (dialogId, TxtStatus, text);
}

static DGUserData EncodeTreeUserData (short catIdx, short subIdx)
{
	const UInt32 c = static_cast<UInt32> (catIdx + 1) & 0xFFu;
	const UInt32 s = (subIdx < 0) ? 0xFFu : (static_cast<UInt32> (subIdx) & 0x7Fu);
	return static_cast<DGUserData> ((c << 8) | s);
}

static bool DecodeTreeUserData (DGUserData data, short& outCatIdx, short& outSubIdx)
{
	const UInt32 val = static_cast<UInt32> (data);
	const UInt32 c = (val >> 8) & 0xFFu;
	if (c == 0) {
		return false;
	}
	outCatIdx = static_cast<short> (c - 1);
	const UInt32 s = val & 0xFFu;
	outSubIdx = (s == 0xFFu) ? -1 : static_cast<short> (s);
	return true;
}

static void AddChildCheckDef (TreeCategoryNode& cat, std::u16string_view name, bool implemented)
{
	TreeChildNode ch;
	ch.subIdx		= static_cast<short> (cat.children.GetSize ());
	ch.baseName		= GS::UniString (name);
	ch.checked		= true;
	ch.implemented	= implemented;
	ch.hasRun		= false;
	ch.issueCount	= 0;
	cat.children.Push (ch);
}

static void InitTreeCategories (void)
{
	if (!gPaletteState.treeCategories.IsEmpty ()) {
		return;
	}

	/* 0: Přesnost modelu */
	{
		TreeCategoryNode cat;
		cat.id		 = CategoryId::Accuracy;
		cat.baseName = GS::UniString (u"Přesnost modelu");
		AddChildCheckDef (cat, u"Rozměry a úhly lineárních prvků", true);
		gPaletteState.treeCategories.Push (cat);
	}

	/* 1: Atributy */
	{
		TreeCategoryNode cat;
		cat.id		 = CategoryId::Attributes;
		cat.baseName = GS::UniString (u"Atributy");
		AddChildCheckDef (cat, u"Chybějící atributy v atributech", true);
		AddChildCheckDef (cat, u"Chybějící textury v povrchových materiálech", true);
		AddChildCheckDef (cat, u"Nezaklasifikované atributy", true);
		AddChildCheckDef (cat, u"Duplikované atributy", true);
		gPaletteState.treeCategories.Push (cat);
	}

	/* 2: Vlastnosti */
	{
		TreeCategoryNode cat;
		cat.id		 = CategoryId::Properties;
		cat.baseName = GS::UniString (u"Vlastnosti");
		AddChildCheckDef (cat, u"Nedostupné pro žádný klas. systém", true);
		gPaletteState.treeCategories.Push (cat);
	}

	/* 3: Prvky modelu */
	{
		TreeCategoryNode cat;
		cat.id		 = CategoryId::Elements;
		cat.baseName = GS::UniString (u"Prvky modelu");
		AddChildCheckDef (cat, u"Chybějící atributy ve vložených prvcích", true);
		AddChildCheckDef (cat, u"Nezaklasifikované prvky", true);
		AddChildCheckDef (cat, u"Prvky bez definované konstrukční funkce", true);
		AddChildCheckDef (cat, u"Prvky bez definovaného umístění", true);
		AddChildCheckDef (cat, u"Prvky ve vrstvě Archicad", true);
		gPaletteState.treeCategories.Push (cat);
	}

	/* 4: Výchozí nastavení nástrojů */
	{
		TreeCategoryNode cat;
		cat.id		 = CategoryId::Defaults;
		cat.baseName = GS::UniString (u"Výchozí nastavení nástrojů");
		AddChildCheckDef (cat, u"Chybějící atributy ve výchozím nastavení", true);
		AddChildCheckDef (cat, u"Nezaklasifikované výchozí nastavení", true);
		AddChildCheckDef (cat, u"Výchozí nastavení bez konstrukční funkce", true);
		AddChildCheckDef (cat, u"Výchozí nastavení bez umístění", true);
		AddChildCheckDef (cat, u"Výchozí nastavení ve vrstvě Archicad", true);
		gPaletteState.treeCategories.Push (cat);
	}

	/* 5: Oblíbená nastavení */
	{
		TreeCategoryNode cat;
		cat.id		 = CategoryId::Favorites;
		cat.baseName = GS::UniString (u"Oblíbená nastavení");
		AddChildCheckDef (cat, u"Chybějící atributy v oblíbených", true);
		AddChildCheckDef (cat, u"Nezaklasifikované oblíbené", true);
		AddChildCheckDef (cat, u"Oblíbené bez konstrukční funkce", true);
		AddChildCheckDef (cat, u"Oblíbené bez umístění", true);
		AddChildCheckDef (cat, u"Oblíbené ve vrstvě Archicad", true);
		gPaletteState.treeCategories.Push (cat);
	}

	/* 6: Zobrazení */
	{
		TreeCategoryNode cat;
		cat.id		 = CategoryId::Views;
		cat.baseName = GS::UniString (u"Zobrazení");
		AddChildCheckDef (cat, u"Nepoužité sady per", true);
		AddChildCheckDef (cat, u"Nepoužité kombinace vrstev", true);
		AddChildCheckDef (cat, u"Nepoužité filtry rekonstrukcí", true);
		AddChildCheckDef (cat, u"Nepoužité 3D styly", true);
		AddChildCheckDef (cat, u"Nepoužité kombinace grafických stylů", true);
		AddChildCheckDef (cat, u"Nepoužité volby zobrazení modelu (VZM)", true);
		gPaletteState.treeCategories.Push (cat);
	}

	/* 7: Dokumentace */
	{
		TreeCategoryNode cat;
		cat.id		 = CategoryId::Docs;
		cat.baseName = GS::UniString (u"Dokumentace");
		AddChildCheckDef (cat, u"Neasociované popisky", true);
		AddChildCheckDef (cat, u"Kóty bez naměřené hodnoty (autotextu)", true);
		AddChildCheckDef (cat, u"Statické části kót", true);
		gPaletteState.treeCategories.Push (cat);
	}
}

static void SyncTreeCountsFromState (void)
{
	if (gPaletteState.treeCategories.GetSize () >= 4) {
		TreeCategoryNode& accCat = gPaletteState.treeCategories[0];
		if (!accCat.children.IsEmpty ()) {
			accCat.children[0].hasRun     = gPaletteState.accuracyHasRun;
			accCat.children[0].issueCount = static_cast<int> (gPaletteState.results.GetSize ());
		}

		TreeCategoryNode& attrCat = gPaletteState.treeCategories[1];
		for (UIndex i = 0; i < attrCat.children.GetSize () && i < gPaletteState.attrSubChecks.GetSize (); ++i) {
			gPaletteState.attrSubChecks[i].checked = attrCat.children[i].checked;
			attrCat.children[i].hasRun             = gPaletteState.attrSubChecks[i].hasRun;
			attrCat.children[i].issueCount         = static_cast<int> (gPaletteState.attrSubChecks[i].rows.GetSize ());
		}

		TreeCategoryNode& propCat = gPaletteState.treeCategories[2];
		for (UIndex i = 0; i < propCat.children.GetSize () && i < gPaletteState.propSubChecks.GetSize (); ++i) {
			gPaletteState.propSubChecks[i].checked = propCat.children[i].checked;
			propCat.children[i].hasRun             = gPaletteState.propSubChecks[i].hasRun;
			propCat.children[i].issueCount         = static_cast<int> (gPaletteState.propSubChecks[i].rows.GetSize ());
		}

		TreeCategoryNode& elemCat = gPaletteState.treeCategories[3];
		for (UIndex i = 0; i < elemCat.children.GetSize () && i < gPaletteState.elemSubChecks.GetSize (); ++i) {
			gPaletteState.elemSubChecks[i].checked = elemCat.children[i].checked;
			elemCat.children[i].hasRun             = gPaletteState.elemSubChecks[i].hasRun;
			elemCat.children[i].issueCount         = static_cast<int> (gPaletteState.elemSubChecks[i].rows.GetSize ());
		}
	}

	if (gPaletteState.treeCategories.GetSize () >= 8) {
		TreeCategoryNode& defCat = gPaletteState.treeCategories[4];
		for (UIndex i = 0; i < defCat.children.GetSize () && i < gPaletteState.defSubChecks.GetSize (); ++i) {
			gPaletteState.defSubChecks[i].checked = defCat.children[i].checked;
			defCat.children[i].hasRun             = gPaletteState.defSubChecks[i].hasRun;
			defCat.children[i].issueCount         = static_cast<int> (gPaletteState.defSubChecks[i].rows.GetSize ());
		}

		TreeCategoryNode& favCat = gPaletteState.treeCategories[5];
		for (UIndex i = 0; i < favCat.children.GetSize () && i < gPaletteState.favSubChecks.GetSize (); ++i) {
			gPaletteState.favSubChecks[i].checked = favCat.children[i].checked;
			favCat.children[i].hasRun             = gPaletteState.favSubChecks[i].hasRun;
			favCat.children[i].issueCount         = static_cast<int> (gPaletteState.favSubChecks[i].rows.GetSize ());
		}

		TreeCategoryNode& viewCat = gPaletteState.treeCategories[6];
		for (UIndex i = 0; i < viewCat.children.GetSize () && i < gPaletteState.viewSubChecks.GetSize (); ++i) {
			gPaletteState.viewSubChecks[i].checked = viewCat.children[i].checked;
			viewCat.children[i].hasRun             = gPaletteState.viewSubChecks[i].hasRun;
			viewCat.children[i].issueCount         = static_cast<int> (gPaletteState.viewSubChecks[i].rows.GetSize ());
		}

		TreeCategoryNode& docCat = gPaletteState.treeCategories[7];
		for (UIndex i = 0; i < docCat.children.GetSize () && i < gPaletteState.docSubChecks.GetSize (); ++i) {
			gPaletteState.docSubChecks[i].checked = docCat.children[i].checked;
			docCat.children[i].hasRun             = gPaletteState.docSubChecks[i].hasRun;
			docCat.children[i].issueCount         = static_cast<int> (gPaletteState.docSubChecks[i].rows.GetSize ());
		}
	}
}

static void UpdateTreeNodesDisplay (short dialogId)
{
	SyncTreeCountsFromState ();

	for (UIndex c = 0; c < gPaletteState.treeCategories.GetSize (); ++c) {
		TreeCategoryNode& cat = gPaletteState.treeCategories[c];
		if (cat.tvItem == DG_TVI_NONE) {
			continue;
		}
		int totalIssues  = 0;
		int checkedCount = 0;
		for (UIndex i = 0; i < cat.children.GetSize (); ++i) {
			const TreeChildNode& ch = cat.children[i];
			totalIssues += ch.issueCount;
			if (ch.checked) {
				++checkedCount;
			}
			if (ch.tvItem != DG_TVI_NONE) {
				const GS::UniString chLabel = ch.baseName + GS::UniString::Printf (" (%d)", ch.issueCount);
				DGTreeViewSetItemText (dialogId, TreeChecks, ch.tvItem, chLabel);
				const short chIcon = ch.checked ? DG_TVIC_CHECKED : DG_TVIC_UNCHECKED;
				DGTreeViewSetItemIcon (dialogId, TreeChecks, ch.tvItem, DG_TVIT_STATE, DG::Icon (SysResModule, chIcon));
			}
		}

		const GS::UniString catLabel = cat.baseName + GS::UniString::Printf (" (%d)", totalIssues);
		DGTreeViewSetItemText (dialogId, TreeChecks, cat.tvItem, catLabel);

		short catIcon = DG_TVIC_UNCHECKED;
		if (checkedCount == static_cast<int> (cat.children.GetSize ()) && checkedCount > 0) {
			catIcon = DG_TVIC_CHECKED;
		} else if (checkedCount > 0) {
			catIcon = DG_TVIC_MIXEDCHECKED;
		}
		DGTreeViewSetItemIcon (dialogId, TreeChecks, cat.tvItem, DG_TVIT_STATE, DG::Icon (SysResModule, catIcon));
	}
}

static void PopulateTreeChecks (short dialogId)
{
	InitTreeCategories ();
	DGTreeViewDisableDraw (dialogId, TreeChecks);
	DGTreeViewDeleteItem (dialogId, TreeChecks, DG_ALL_ITEMS);

	Int32 itemToSelect = DG_TVI_NONE;

	for (UIndex c = 0; c < gPaletteState.treeCategories.GetSize (); ++c) {
		TreeCategoryNode& cat = gPaletteState.treeCategories[c];
		cat.tvItem = DGTreeViewInsertItem (dialogId, TreeChecks, DG_TVI_ROOT, DG_TVI_BOTTOM);
		DGTreeViewSetItemUserData (dialogId, TreeChecks, cat.tvItem, EncodeTreeUserData (static_cast<short> (c), -1));

		if (static_cast<short> (gPaletteState.selectedCategory) == static_cast<short> (c) && gPaletteState.selectedSubIdx < 0) {
			itemToSelect = cat.tvItem;
		}

		for (UIndex i = 0; i < cat.children.GetSize (); ++i) {
			TreeChildNode& ch = cat.children[i];
			ch.tvItem = DGTreeViewInsertItem (dialogId, TreeChecks, cat.tvItem, DG_TVI_BOTTOM);
			DGTreeViewSetItemUserData (dialogId, TreeChecks, ch.tvItem,
				EncodeTreeUserData (static_cast<short> (c), static_cast<short> (i)));

			if (static_cast<short> (gPaletteState.selectedCategory) == static_cast<short> (c) &&
				gPaletteState.selectedSubIdx == static_cast<short> (i)) {
				itemToSelect = ch.tvItem;
			}
		}
		DGTreeViewExpandItem (dialogId, TreeChecks, cat.tvItem);
	}

	UpdateTreeNodesDisplay (dialogId);
	DGTreeViewEnableDraw (dialogId, TreeChecks);

	if (itemToSelect != DG_TVI_NONE) {
		DGTreeViewSelectItem (dialogId, TreeChecks, itemToSelect);
	}
}

static void SetAllTreeChecks (short dialogId, bool checked)
{
	for (TreeCategoryNode& cat : gPaletteState.treeCategories) {
		for (TreeChildNode& ch : cat.children) {
			ch.checked = checked;
		}
	}
	UpdateTreeNodesDisplay (dialogId);
	RefreshResultList (dialogId);
}

static void ToggleTreeItemCheckbox (short dialogId, Int32 clickedTvItem)
{
	short catIdx = -1;
	short subIdx = -1;
	if (!DecodeTreeUserData (DGTreeViewGetItemUserData (dialogId, TreeChecks, clickedTvItem), catIdx, subIdx)) {
		return;
	}
	if (catIdx < 0 || static_cast<UIndex> (catIdx) >= gPaletteState.treeCategories.GetSize ()) {
		return;
	}
	TreeCategoryNode& cat = gPaletteState.treeCategories[catIdx];
	if (subIdx < 0) {
		/* Kliknuto na nadřazenou kategorii: pokud jsou všechny zaškrtnuté -> odškrtnout vše, jinak zaškrtnout vše */
		bool allChecked = true;
		for (const TreeChildNode& ch : cat.children) {
			if (!ch.checked) {
				allChecked = false;
				break;
			}
		}
		const bool newState = !allChecked;
		for (TreeChildNode& ch : cat.children) {
			ch.checked = newState;
		}
	} else if (static_cast<UIndex> (subIdx) < cat.children.GetSize ()) {
		cat.children[subIdx].checked = !cat.children[subIdx].checked;
	}
	UpdateTreeNodesDisplay (dialogId);
	if (gPaletteState.selectedSubIdx < 0 && static_cast<short> (gPaletteState.selectedCategory) == catIdx) {
		RefreshResultList (dialogId);
	}
}

static void UpdateModeControls (short dialogId)
{
	if (gPaletteState.selectedCategory != CategoryId::Accuracy) {
		return;
	}
	const bool hairy = DGGetItemValLong (dialogId, RadHairy) != 0;
	if (hairy) {
		DGShowItem (dialogId, TxtTolLength);
		DGShowItem (dialogId, EditToleranceMm);
		DGShowItem (dialogId, BtnInfoTolLength);
		DGShowItem (dialogId, TxtTolAngle);
		DGShowItem (dialogId, EditAngleTolerance);
		DGShowItem (dialogId, BtnInfoTolAngle);
		DGHideItem (dialogId, TxtModule);
		DGHideItem (dialogId, EditModuleMm);
	} else {
		DGHideItem (dialogId, TxtTolLength);
		DGHideItem (dialogId, EditToleranceMm);
		DGHideItem (dialogId, BtnInfoTolLength);
		DGHideItem (dialogId, TxtTolAngle);
		DGHideItem (dialogId, EditAngleTolerance);
		DGHideItem (dialogId, BtnInfoTolAngle);
		DGShowItem (dialogId, TxtModule);
		DGShowItem (dialogId, EditModuleMm);
	}
	if (gPaletteState.dialogId == dialogId) {
		RefreshResultList (dialogId);
	}
}

static void AdjustResultListTopPosition (short dialogId, short desiredTop)
{
	short left = 0, top = 0, right = 0, bottom = 0;
	DGGetItemRect (dialogId, ResultList, &left, &top, &right, &bottom);
	const short dy = desiredTop - top;
	if (dy != 0) {
		DGBeginMoveGrowItems (dialogId);
		DGMoveGrowItem (dialogId, ResultList, 0, dy, 0, -dy);
		DGEndMoveGrowItems (dialogId);
	}
}

static void UpdateRightPanelLayout (short dialogId)
{
	const short accuracyItems[] = {
		TxtTools, BtnWall, BtnBeam, BtnColumn, BtnLine, BtnPolyLine,
		ChkSelectedOnly, RadActiveView, RadWholeModel, RadHairy, RadModular,
		TxtModule, EditModuleMm, TxtTolLength, EditToleranceMm,
		TxtTolAngle, EditAngleTolerance, BtnInfoTolLength, BtnInfoTolAngle
	};

	const short catIdx = static_cast<short> (gPaletteState.selectedCategory);
	const short subIdx = gPaletteState.selectedSubIdx;

	GS::UniString headerTitle;
	if (catIdx >= 0 && static_cast<UIndex> (catIdx) < gPaletteState.treeCategories.GetSize ()) {
		const TreeCategoryNode& cat = gPaletteState.treeCategories[catIdx];
		headerTitle = cat.baseName;
		if (subIdx >= 0 && static_cast<UIndex> (subIdx) < cat.children.GetSize ()) {
			headerTitle += GS::UniString (u" — ") + cat.children[subIdx].baseName;
		}
	}
	DGSetItemText (dialogId, TxtRightHeader, headerTitle);

	if (gPaletteState.selectedCategory == CategoryId::Accuracy) {
		DGHideItem (dialogId, TxtSubChecksHdr);
		for (short id : accuracyItems) {
			DGShowItem (dialogId, id);
		}
		AdjustResultListTopPosition (dialogId, 256);
		UpdateModeControls (dialogId);
	} else {
		for (short id : accuracyItems) {
			DGHideItem (dialogId, id);
		}
		if (subIdx < 0) {
			DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Přehled dílčích kontrol v kategorii (poklikáním na řádek zobrazíte podrobné výsledky):"));
		} else if (gPaletteState.selectedCategory == CategoryId::Attributes) {
			if (subIdx == static_cast<short> (AttributeSubCheckId::UnclassifiedAttributes)) {
				DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Klasifikační systémy zahrnuté do kontroly lze zvolit pod tlačítkem Nastavení..."));
			} else {
				DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Nalezené problematické atributy v projektu:"));
			}
		} else if (gPaletteState.selectedCategory == CategoryId::Elements) {
			if (subIdx == static_cast<short> (ElementSubCheckId::UnclassifiedElements)) {
				DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Klasifikační systémy zahrnuté do kontroly lze zvolit pod tlačítkem Nastavení... Poklikáním přiblížíte na prvek:"));
			} else {
				DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Nalezené problematické prvky v modelu (poklikáním přiblížíte na prvek, pravým tlačítkem otevřete nabídku 2D/3D):"));
			}
		} else if (gPaletteState.selectedCategory == CategoryId::Defaults) {
			if (subIdx == static_cast<short> (ElementSubCheckId::UnclassifiedElements)) {
				DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Klasifikační systémy zahrnuté do kontroly lze zvolit pod tlačítkem Nastavení..."));
			} else {
				DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Nalezené problémy ve výchozím nastavení nástrojů:"));
			}
		} else if (gPaletteState.selectedCategory == CategoryId::Favorites) {
			if (subIdx == static_cast<short> (ElementSubCheckId::UnclassifiedElements)) {
				DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Klasifikační systémy zahrnuté do kontroly lze zvolit pod tlačítkem Nastavení..."));
			} else {
				DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Nalezené problémy v oblíbených nastaveních projektu:"));
			}
		} else if (gPaletteState.selectedCategory == CategoryId::Properties) {
			DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Vlastní vlastnosti projektu, které nejsou zpřístupněny pro žádnou položku klasifikace:"));
		} else if (gPaletteState.selectedCategory == CategoryId::Views) {
			DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Nastavení zobrazení, která nejsou použita v žádném zobrazení v Mapě zobrazení:"));
		} else if (gPaletteState.selectedCategory == CategoryId::Docs) {
			DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Nalezené problematické 2D dokumentační prvky (poklikáním přiblížíte na prvek, pravým tlačítkem otevřete nabídku):"));
		} else {
			DGSetItemText (dialogId, TxtSubChecksHdr, GS::UniString (u"Přehled výsledků zvolené kontroly:"));
		}
		DGShowItem (dialogId, TxtSubChecksHdr);
		AdjustResultListTopPosition (dialogId, 66);
		RefreshResultList (dialogId);
	}
}

static GS::UniString GetToleranceHelpText (void)
{
	return GS::UniString (
		u"CHLUPATÁ ČÍSLA — TOLERANCE DÉLKY A VÝŠKY [mm]\n"
		u"Kontroluje, zda je skutečná délka či výška prvku dostatečně blízko celému číslu v milimetrech.\n"
		u"Prvek je vyhodnocen jako nepřesný, pokud je odchylka od nejbližšího celého mm VĚTŠÍ než tolerance.\n\n"
		u"Příklad (tolerance 0,1 mm):\n"
		u"  Rozměr 2500,05 mm → nejbližší celé 2500 mm, odchylka 0,05 mm → v pořádku.\n"
		u"  Rozměr 2500,15 mm → odchylka 0,15 mm > 0,1 mm → problém.\n\n"
		u"CHLUPATÁ ČÍSLA — TOLERANCE ÚHLU [°]\n"
		u"Úhel se porovnává s nejbližším celým stupněm (0–180°). Problém nastane, pokud odchylka překročí toleranci.\n\n"
		u"Příklad (tolerance 0,01°):\n"
		u"  Úhel 45,005° → v pořádku; úhel 45,02° → problém.\n\n"
		u"MODULARITA\n"
		u"V režimu Modularita se kontroluje, zda jsou délky a výšky celočíselným násobkem zadaného modulu [mm].\n\n"
		u"DESETINNÁ ČÁRKA A TEČKA\n"
		u"Do polí můžete zadávat desetinnou čárku i tečku (např. 0,1 i 0.1).\n"
		u"Počet desetinných míst zobrazovaných v tabulce výsledků lze upravit v okně Nastavení (výchozí 4).");
}

static short DGCALLBACK ToleranceHelpDialogHandler (short message, short dialogId, short item, DGUserData, DGMessageData)
{
	switch (message) {
		case DG_MSG_INIT:
			DGSetItemText (dialogId, ToleranceHelpTxtBody, GetToleranceHelpText ());
			return 0;
		case DG_MSG_CLICK:
			if (item == ToleranceHelpBtnOk || item == DG_CLOSEBOX) {
				return item;
			}
			return 0;
		default:
			break;
	}
	return 0;
}

static void ShowToleranceHelpDialog (void)
{
	(void) DGModalDialog (ACAPI_GetOwnResModule (), CHLUPATE_TOLERANCE_HELP_ID, ACAPI_GetOwnResModule (), ToleranceHelpDialogHandler, 0);
}

static bool Is3DElementType (API_ElemTypeID type)
{
	return type == API_WallID || type == API_BeamID || type == API_ColumnID;
}

static GS::UniString GetTypeNameCZ (API_ElemTypeID typeId)
{
	switch (typeId) {
		case API_WallID:		return GS::UniString (u"Zeď");
		case API_BeamID:		return GS::UniString (u"Trám");
		case API_ColumnID:		return GS::UniString (u"Sloup");
		case API_LineID:		return GS::UniString (u"Čára");
		case API_PolyLineID:	return GS::UniString (u"Lomená čára");
		default:				return GS::UniString (u"Neznámý");
	}
}

static GS::UniString FormatFixedDecimals (double value, int decimals)
{
	const int d = std::clamp (decimals, 0, 10);
	std::ostringstream stream;
	stream.imbue (std::locale::classic ());
	stream << std::fixed << std::setprecision (d) << value;
	std::string str = stream.str ();
	for (char& c : str) {
		if (c == '.') {
			c = ',';
		}
	}
	return GS::UniString (str.c_str (), CC_UTF8);
}

static bool ParseUserDouble (const GS::UniString& text, double& out)
{
	if (text.IsEmpty ()) {
		return false;
	}
	const auto	utf8	= text.ToCStr (0, MaxUSize, CC_UTF8);
	const char*	p		= utf8.Get ();
	if (p == nullptr) {
		return false;
	}
	std::string t (p);
	for (char& c : t) {
		if (c == ',') {
			c = '.';
		}
	}
	if (t.empty ()) {
		return false;
	}
	char*		endPtr	= nullptr;
	const double v		= std::strtod (t.c_str (), &endPtr);
	if (endPtr == t.c_str ()) {
		return false;
	}
	out = v;
	return true;
}

static bool GetDialogTextDouble (short dialogId, short item, double& out, double defVal = 0.0)
{
	const GS::UniString s = DGGetItemText (dialogId, item);
	if (s.IsEmpty ()) {
		out = defVal;
		return true;
	}
	return ParseUserDouble (s, out);
}

static void BuildStoryNameMap (GS::HashTable<short, GS::UniString>& outStoryMap)
{
	outStoryMap.Clear ();
	API_StoryInfo storyInfo = {};
	if (ACAPI_ProjectSetting_GetStorySettings (&storyInfo) != NoError || storyInfo.data == nullptr) {
		return;
	}
	const short nStories = (storyInfo.lastStory - storyInfo.firstStory + 1);
	for (short idx = 0; idx < nStories; ++idx) {
		const short floorInd = static_cast<short> (storyInfo.firstStory + idx);
		const auto* row = &(*storyInfo.data)[idx];
		if (GS::ucslen (row->uName) > 0) {
			outStoryMap.Put (floorInd, GS::UniString (row->uName));
		} else {
			outStoryMap.Put (floorInd, GS::ValueToUniString (static_cast<int> (floorInd)) + GS::UniString (u". podlaží"));
		}
	}
	BMKillHandle ((GSHandle*) &storyInfo.data);
}

static GS::UniString LookupHomeStoryName (short floorInd, const GS::HashTable<short, GS::UniString>& storyMap)
{
	if (storyMap.ContainsKey (floorInd)) {
		return storyMap[floorInd];
	}
	return GS::UniString (u"(podlaží ") + GS::ValueToUniString (static_cast<int> (floorInd)) + GS::UniString (u")");
}

static void GetLayerNameAndHidden (API_AttributeIndex layerInd, GS::UniString& outName, bool& outHidden)
{
	API_Attribute attr;
	BNZeroMemory (&attr, sizeof (API_Attribute));
	attr.header.typeID	= API_LayerID;
	attr.header.index	= layerInd;
	if (ACAPI_Attribute_Get (&attr) != NoError) {
		outName		= GS::UniString (u"(vrstva ?)");
		outHidden	= false;
		return;
	}
	if (attr.header.uniStringNamePtr != nullptr) {
		outName = *attr.header.uniStringNamePtr;
	} else {
		outName = GS::UniString (attr.layer.head.name, CC_Default);
	}
	outHidden = (attr.layer.head.flags & APILay_Hidden) != 0;
}

static double Distance2D (const API_Coord& a, const API_Coord& b)
{
	const double dx = b.x - a.x;
	const double dy = b.y - a.y;
	return std::sqrt (dx * dx + dy * dy);
}

static double NormalizeAngle180 (double angleDeg)
{
	double angle = std::fmod (angleDeg, 180.0);
	if (angle < 0.0) {
		angle += 180.0;
	}
	if (angle >= 180.0 - 1e-12) {
		angle = 0.0;
	}
	return angle;
}

static double CalculateAngleDeg (const API_Coord& a, const API_Coord& b)
{
	const double angleRad = std::atan2 (b.y - a.y, b.x - a.x);
	return NormalizeAngle180 (angleRad * 180.0 / kPi);
}

static GSErrCode GetSelectedGuids (GS::HashSet<API_Guid>& selectedGuids)
{
	API_SelectionInfo	selectionInfo = {};
	GS::Array<API_Neig>	selNeigs;
	const GSErrCode		err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, false, false);
	if (err == APIERR_NOSEL || selectionInfo.typeID == API_SelEmpty) {
		return NoError;
	}
	if (err != NoError) {
		return err;
	}
	for (const API_Neig& neig : selNeigs) {
		selectedGuids.Add (neig.guid);
	}
	BMKillHandle ((GSHandle*) &selectionInfo.marquee.coords);
	return NoError;
}

/* Chlupatá čísla: odchylka od celého [mm] musí být VĚTŠÍ než tolerance (0,10 s tol. 0,1 je v pořádku). */
static bool HairyLengthFails (double valueMm, double toleranceMm, double& outDeviation)
{
	const double target	= std::round (valueMm);
	outDeviation		= std::fabs (valueMm - target);
	return outDeviation > (toleranceMm + 1e-9);
}

/* Modularita: rozměr není celý násobek modulu. */
static bool ModularLengthFails (double valueMm, double moduleMm)
{
	if (moduleMm <= 1e-9) {
		return false;
	}
	const double a		= std::abs (valueMm);
	const double m		= std::abs (moduleMm);
	const double r		= std::fmod (a, m);
	const double eps	= 1e-3; /* 0,001 mm numerická tolerance */
	if (r < eps || (m - r) < eps) {
		return false;
	}
	return true;
}

static bool IsAngleOut (double angleDeg, double toleranceDeg, double& outDeviation)
{
	const double nearest	= std::round (angleDeg);
	outDeviation			= std::fabs (angleDeg - nearest);
	return outDeviation > (toleranceDeg + 1e-9);
}

static short ChlupResultListColumnCount (bool hairy)
{
	/* 7: typ, parametr, rozměr, úhel, podlaží, stav(ikona), vrstva | 6: bez úhlu. */
	return hairy ? 7 : 6;
}

static UShort ChlupByteToU (UInt32 b)
{
	return static_cast<UShort> (b * 257u);
}

static void ChlupApplyListTabFieldLayout (short dialogId, bool hairy)
{
	const short n = ChlupResultListColumnCount (hairy);
	DGListSetTabFieldCount (dialogId, ResultList, n);
	short x = 0;
	if (hairy) {
		const short w1 = 80;
		const short w2 = 150;
		const short w3 = 115;
		const short w4 = 95;
		const short w5 = 115;
		const short w6 = 44;
		const short w7 = 195;
		DGListSetTabFieldData (dialogId, ResultList, 1, x, x + w1, DG_IS_LEFT, DG_IS_TRUNCEND, true, true); x += w1;
		DGListSetTabFieldData (dialogId, ResultList, 2, x, x + w2, DG_IS_LEFT, DG_IS_TRUNCEND, true, true); x += w2;
		DGListSetTabFieldData (dialogId, ResultList, 3, x, x + w3, DG_IS_LEFT, DG_IS_TRUNCEND, true, true); x += w3;
		DGListSetTabFieldData (dialogId, ResultList, 4, x, x + w4, DG_IS_LEFT, DG_IS_TRUNCEND, true, true); x += w4;
		DGListSetTabFieldData (dialogId, ResultList, 5, x, x + w5, DG_IS_LEFT, DG_IS_TRUNCEND, true, true); x += w5;
		DGListSetTabFieldData (dialogId, ResultList, 6, x, x + w6, DG_IS_CENTER, DG_IS_TRUNCEND, true, true); x += w6;
		DGListSetTabFieldData (dialogId, ResultList, 7, x, x + w7, DG_IS_LEFT, DG_IS_TRUNCEND, false, true);
	} else {
		const short w1 = 88;
		const short w2 = 160;
		const short w3 = 130;
		const short w4 = 130;
		const short w5 = 44;
		const short w6 = 240;
		DGListSetTabFieldData (dialogId, ResultList, 1, x, x + w1, DG_IS_LEFT, DG_IS_TRUNCEND, true, true); x += w1;
		DGListSetTabFieldData (dialogId, ResultList, 2, x, x + w2, DG_IS_LEFT, DG_IS_TRUNCEND, true, true); x += w2;
		DGListSetTabFieldData (dialogId, ResultList, 3, x, x + w3, DG_IS_LEFT, DG_IS_TRUNCEND, true, true); x += w3;
		DGListSetTabFieldData (dialogId, ResultList, 4, x, x + w4, DG_IS_LEFT, DG_IS_TRUNCEND, true, true); x += w4;
		DGListSetTabFieldData (dialogId, ResultList, 5, x, x + w5, DG_IS_CENTER, DG_IS_TRUNCEND, true, true); x += w5;
		DGListSetTabFieldData (dialogId, ResultList, 6, x, x + w6, DG_IS_LEFT, DG_IS_TRUNCEND, false, true);
	}
}

static void ApplyResultListHeaders (short dialogId, bool hairy)
{
	const short n = ChlupResultListColumnCount (hairy);
	ChlupApplyListTabFieldLayout (dialogId, hairy);
	DGListSetHeaderItemCount (dialogId, ResultList, n);
	const GS::UniString hType (u"Typ prvku");
	const GS::UniString hParam (u"Kontrolovaný parametr");
	const GS::UniString hDim (u"Rozměr [mm]");
	const GS::UniString hAngle (u"Úhel [°]");
	const GS::UniString hStory (u"Domovské podlaží");
	const GS::UniString hState (u"Stav vrstvy");
	const GS::UniString hLayer (u"Vrstva");
	if (hairy) {
		DGListSetHeaderItemText (dialogId, ResultList, 1, hType);
		DGListSetHeaderItemText (dialogId, ResultList, 2, hParam);
		DGListSetHeaderItemText (dialogId, ResultList, 3, hDim);
		DGListSetHeaderItemText (dialogId, ResultList, 4, hAngle);
		DGListSetHeaderItemText (dialogId, ResultList, 5, hStory);
		DGListSetHeaderItemText (dialogId, ResultList, 6, hState);
		DGListSetHeaderItemText (dialogId, ResultList, 7, hLayer);
		DGListSetHeaderItemSize (dialogId, ResultList, 1, 80);
		DGListSetHeaderItemSize (dialogId, ResultList, 2, 150);
		DGListSetHeaderItemSize (dialogId, ResultList, 3, 115);
		DGListSetHeaderItemSize (dialogId, ResultList, 4, 95);
		DGListSetHeaderItemSize (dialogId, ResultList, 5, 115);
		DGListSetHeaderItemSize (dialogId, ResultList, 6, 44);
		DGListSetHeaderItemSize (dialogId, ResultList, 7, 195);
	} else {
		DGListSetHeaderItemText (dialogId, ResultList, 1, hType);
		DGListSetHeaderItemText (dialogId, ResultList, 2, hParam);
		DGListSetHeaderItemText (dialogId, ResultList, 3, hDim);
		DGListSetHeaderItemText (dialogId, ResultList, 4, hStory);
		DGListSetHeaderItemText (dialogId, ResultList, 5, hState);
		DGListSetHeaderItemText (dialogId, ResultList, 6, hLayer);
		DGListSetHeaderItemSize (dialogId, ResultList, 1, 88);
		DGListSetHeaderItemSize (dialogId, ResultList, 2, 160);
		DGListSetHeaderItemSize (dialogId, ResultList, 3, 130);
		DGListSetHeaderItemSize (dialogId, ResultList, 4, 130);
		DGListSetHeaderItemSize (dialogId, ResultList, 5, 44);
		DGListSetHeaderItemSize (dialogId, ResultList, 6, 240);
	}
	DGListSetHeaderFlags (dialogId, ResultList, false, false, false);
	DGListEnableDraw (dialogId, ResultList);
}

static void ApplyAuditResultListHeaders (short dialogId, const GS::Array<AuditColumnDef>& cols)
{
	short left = 0, top = 0, right = 0, bottom = 0;
	DGGetItemRect (dialogId, ResultList, &left, &top, &right, &bottom);
	const short listWidth = std::max<short> (200, static_cast<short> ((right - left) - 18));

	const short n = static_cast<short> (cols.GetSize ());
	if (n <= 0) {
		DGListSetTabFieldCount (dialogId, ResultList, 1);
		DGListSetTabFieldData (dialogId, ResultList, 1, 0, listWidth, DG_IS_LEFT, DG_IS_TRUNCEND, false, true);
		DGListSetHeaderItemCount (dialogId, ResultList, 1);
		DGListSetHeaderItemText (dialogId, ResultList, 1, GS::UniString (u"Výsledek"));
		DGListSetHeaderItemSize (dialogId, ResultList, 1, listWidth);
		return;
	}
	DGListSetTabFieldCount (dialogId, ResultList, n);
	DGListSetHeaderItemCount (dialogId, ResultList, n);
	short x = 0;
	for (short i = 0; i < n; ++i) {
		const bool isLast = (i + 1 == n);
		const short w = isLast
			? std::max<short> (60, static_cast<short> (listWidth - x))
			: cols[i].width;
		const bool hasSep = !isLast;
		DGListSetTabFieldData (dialogId, ResultList, i + 1, x, x + w, DG_IS_LEFT, DG_IS_TRUNCEND, hasSep, true);
		DGListSetHeaderItemText (dialogId, ResultList, i + 1, cols[i].title);
		DGListSetHeaderItemSize (dialogId, ResultList, i + 1, w);
		x += w;
	}
	DGListSetHeaderFlags (dialogId, ResultList, false, false, false);
	DGListEnableDraw (dialogId, ResultList);
}

static void ChlupClearListRowHighlights (short dialogId)
{
	const short rows = DGListGetItemCount (dialogId, ResultList);
	for (short i = 1; i <= rows; ++i) {
		DGListResetItemBackgroundColor (dialogId, ResultList, i);
	}
}

static const GS::Array<SubCheckState>* GetSubChecksForCategory (CategoryId cat)
{
	switch (cat) {
		case CategoryId::Attributes:	return &gPaletteState.attrSubChecks;
		case CategoryId::Properties:	return &gPaletteState.propSubChecks;
		case CategoryId::Elements:		return &gPaletteState.elemSubChecks;
		case CategoryId::Defaults:		return &gPaletteState.defSubChecks;
		case CategoryId::Favorites:		return &gPaletteState.favSubChecks;
		case CategoryId::Views:			return &gPaletteState.viewSubChecks;
		case CategoryId::Docs:			return &gPaletteState.docSubChecks;
		default:						return nullptr;
	}
}

static void ChlupApplyListRowHighlighting (short dialogId)
{
	ChlupClearListRowHighlights (dialogId);
	if (gChlupListHighlightGuid == APINULLGuid) {
		return;
	}
	if (gPaletteState.selectedCategory == CategoryId::Accuracy) {
		const short curSel = DGListGetSelected (dialogId, ResultList, 0);
		bool firstSelected = (curSel > 0 && static_cast<UIndex> (curSel - 1) < gPaletteState.results.GetSize () &&
							  gPaletteState.results[curSel - 1].guid == gChlupListHighlightGuid);
		for (UIndex i = 0; i < gPaletteState.results.GetSize (); ++i) {
			if (gPaletteState.results[i].guid != gChlupListHighlightGuid) {
				continue;
			}
			const short row = static_cast<short> (i + 1);
			DGListSetItemBackgroundColor (dialogId, ResultList, row, ChlupByteToU (0xB8u), ChlupByteToU (0xE8u), ChlupByteToU (0xB0u));
			if (!firstSelected) {
				DGListSelectItem (dialogId, ResultList, row);
				firstSelected = true;
			}
		}
	} else if ((gPaletteState.selectedCategory == CategoryId::Elements ||
				gPaletteState.selectedCategory == CategoryId::Docs) &&
			   gPaletteState.selectedSubIdx >= 0) {
		const GS::Array<SubCheckState>* subChecksPtr = GetSubChecksForCategory (gPaletteState.selectedCategory);
		if (subChecksPtr != nullptr && static_cast<UIndex> (gPaletteState.selectedSubIdx) < subChecksPtr->GetSize ()) {
			const SubCheckState& sub = (*subChecksPtr)[gPaletteState.selectedSubIdx];
			const short curSel = DGListGetSelected (dialogId, ResultList, 0);
			bool firstSelected = (curSel > 0 && static_cast<UIndex> (curSel - 1) < sub.rows.GetSize () &&
								  sub.rows[curSel - 1].elemGuid == gChlupListHighlightGuid);
			for (UIndex i = 0; i < sub.rows.GetSize (); ++i) {
				if (sub.rows[i].elemGuid != gChlupListHighlightGuid) {
					continue;
				}
				const short row = static_cast<short> (i + 1);
				DGListSetItemBackgroundColor (dialogId, ResultList, row, ChlupByteToU (0xB8u), ChlupByteToU (0xE8u), ChlupByteToU (0xB0u));
				if (!firstSelected) {
					DGListSelectItem (dialogId, ResultList, row);
					firstSelected = true;
				}
			}
		}
	}
}

static GSErrCode ChlupOnSelectionChange (const API_Neig* /*one*/)
{
	if (gIsSyncingDimPens || gPaletteState.dialogId == 0 ||
		(gPaletteState.selectedCategory != CategoryId::Accuracy &&
		 gPaletteState.selectedCategory != CategoryId::Elements &&
		 gPaletteState.selectedCategory != CategoryId::Docs)) {
		return NoError;
	}
	API_SelectionInfo	selectionInfo = {};
	GS::Array<API_Neig>	selNeigs;
	const GSErrCode		err	= ACAPI_Selection_Get (&selectionInfo, &selNeigs, false, false);
	if (err != NoError && err != APIERR_NOSEL) {
		BMKillHandle ((GSHandle*) &selectionInfo.marquee.coords);
		return NoError;
	}
	BMKillHandle ((GSHandle*) &selectionInfo.marquee.coords);

	const API_Guid prevHighlightGuid = gChlupListHighlightGuid;
	gChlupListHighlightGuid = APINULLGuid;
	for (UIndex s = 0; s < selNeigs.GetSize (); ++s) {
		const API_Guid& g = selNeigs[s].guid;
		if (gPaletteState.selectedCategory == CategoryId::Accuracy) {
			for (UIndex r = 0; r < gPaletteState.results.GetSize (); ++r) {
				if (gPaletteState.results[r].guid == g) {
					gChlupListHighlightGuid = g;
					break;
				}
			}
		} else if (gPaletteState.selectedSubIdx >= 0) {
			const GS::Array<SubCheckState>* subChecksPtr = GetSubChecksForCategory (gPaletteState.selectedCategory);
			if (subChecksPtr != nullptr && static_cast<UIndex> (gPaletteState.selectedSubIdx) < subChecksPtr->GetSize ()) {
				const SubCheckState& sub = (*subChecksPtr)[gPaletteState.selectedSubIdx];
				for (UIndex r = 0; r < sub.rows.GetSize (); ++r) {
					if (sub.rows[r].elemGuid == g) {
						gChlupListHighlightGuid = g;
						break;
					}
				}
			}
		}
		if (gChlupListHighlightGuid != APINULLGuid) {
			break;
		}
	}
	ChlupApplyListRowHighlighting (gPaletteState.dialogId);

	if (gPaletteState.selectedCategory == CategoryId::Docs &&
		(gPaletteState.selectedSubIdx == static_cast<short> (DocSubCheckId::CustomTextDimensions) ||
		 gPaletteState.selectedSubIdx == static_cast<short> (DocSubCheckId::StaticDimensions))) {
		if (prevHighlightGuid != APINULLGuid && gChlupListHighlightGuid == APINULLGuid && gSelectedCustomDimRowIndex != -1) {
			DGListDeselectItem (gPaletteState.dialogId, ResultList, DG_ALL_ITEMS);
			gSelectedCustomDimRowIndex = -1;
			const bool liveSync = (DGGetItemValLong (gPaletteState.dialogId, ChkLiveSync) != 0);
			if (liveSync || !gDimPenBackup.IsEmpty () || !gStaticDimCircles.IsEmpty ()) {
				RecolorModelByCurrentTab (gPaletteState.dialogId, !liveSync);
			}
		}
	}
	return NoError;
}

static void RefreshResultList (short dialogId)
{

	DGListDeleteItem (dialogId, ResultList, DG_ALL_ITEMS);

	if (gPaletteState.selectedCategory == CategoryId::Accuracy) {
		const bool hairy = (DGGetItemValLong (dialogId, RadHairy) != 0);
		ApplyResultListHeaders (dialogId, hairy);
		const GS::UniString dashStr (u"—");

		for (UIndex i = 0; i < gPaletteState.results.GetSize (); ++i) {
			const ScanResult& r = gPaletteState.results[i];
			(void) DGListInsertItem (dialogId, ResultList, DG_LIST_BOTTOM);
			const short rowPos = DGListGetItemCount (dialogId, ResultList);
			if (rowPos <= 0) {
				continue;
			}
			const short		iconRes	= r.layerHidden ? Cegra_Icon_LayerHide : Cegra_Icon_LayerShow;
			const DG::Icon	layIcon (ACAPI_GetOwnResModule (), iconRes);
			if (hairy) {
				DGListSetTabItemText (dialogId, ResultList, rowPos, 1, GetTypeNameCZ (r.type));
				DGListSetTabItemText (dialogId, ResultList, rowPos, 2, r.checkedParam);
				DGListSetTabItemText (dialogId, ResultList, rowPos, 3, FormatFixedDecimals (r.lengthMm, gListDecimals));
				DGListSetTabItemText (dialogId, ResultList, rowPos, 4, r.hasAngleValue ? FormatFixedDecimals (r.angleDeg, gListDecimals) : dashStr);
				DGListSetTabItemText (dialogId, ResultList, rowPos, 5, r.homeStory);
				DGListSetTabItemText (dialogId, ResultList, rowPos, 6, GS::EmptyUniString);
				DGListSetTabItemIcon (dialogId, ResultList, rowPos, 6, layIcon);
				DGListSetTabItemText (dialogId, ResultList, rowPos, 7, r.layerName);
			} else {
				DGListSetTabItemText (dialogId, ResultList, rowPos, 1, GetTypeNameCZ (r.type));
				DGListSetTabItemText (dialogId, ResultList, rowPos, 2, r.checkedParam);
				DGListSetTabItemText (dialogId, ResultList, rowPos, 3, FormatFixedDecimals (r.lengthMm, gListDecimals));
				DGListSetTabItemText (dialogId, ResultList, rowPos, 4, r.homeStory);
				DGListSetTabItemText (dialogId, ResultList, rowPos, 5, GS::EmptyUniString);
				DGListSetTabItemIcon (dialogId, ResultList, rowPos, 5, layIcon);
				DGListSetTabItemText (dialogId, ResultList, rowPos, 6, r.layerName);
			}
			DGListSetItemUserData (dialogId, ResultList, rowPos, static_cast<DGUserData> (i + 1));
		}
		ChlupApplyListRowHighlighting (dialogId);
		return;
	}

	const short catIdx = static_cast<short> (gPaletteState.selectedCategory);
	const short subIdx = gPaletteState.selectedSubIdx;

	/* Pokud je ve stromu vybrána nadřazená kategorie (subIdx < 0), zobrazíme přehled jejích dílčích kontrol */
	if (subIdx < 0 && catIdx >= 0 && static_cast<UIndex> (catIdx) < gPaletteState.treeCategories.GetSize ()) {
		GS::Array<AuditColumnDef> summaryCols;
		summaryCols.Push ({ GS::UniString (u"Dílčí kontrola"), 420 });
		summaryCols.Push ({ GS::UniString (u"Režim"), 120 });
		summaryCols.Push ({ GS::UniString (u"Nalezeno problémů"), 180 });
		ApplyAuditResultListHeaders (dialogId, summaryCols);

		const TreeCategoryNode& cat = gPaletteState.treeCategories[catIdx];
		for (UIndex i = 0; i < cat.children.GetSize (); ++i) {
			const TreeChildNode& ch = cat.children[i];
			(void) DGListInsertItem (dialogId, ResultList, DG_LIST_BOTTOM);
			const short rowPos = DGListGetItemCount (dialogId, ResultList);
			if (rowPos <= 0) {
				continue;
			}
			DGListSetTabItemText (dialogId, ResultList, rowPos, 1, ch.baseName);
			DGListSetTabItemText (dialogId, ResultList, rowPos, 2,
				ch.checked
					? GS::UniString (u"Aktivní")
					: GS::UniString (u"Vypnuto"));

			GS::UniString countText;
			if (!ch.implemented) {
				countText = GS::UniString (u"Připravuje se");
			} else if (ch.hasRun) {
				countText = GS::UniString::Printf ("%d", ch.issueCount);
			} else {
				countText = GS::UniString (u"0 (nezkontrolováno)");
			}
			DGListSetTabItemText (dialogId, ResultList, rowPos, 3, countText);
			DGListSetItemUserData (dialogId, ResultList, rowPos, static_cast<DGUserData> (i + 1));
		}
		return;
	}

	const GS::Array<SubCheckState>* subChecksPtr = GetSubChecksForCategory (gPaletteState.selectedCategory);
	if (subChecksPtr != nullptr && subIdx >= 0 && static_cast<UIndex> (subIdx) < subChecksPtr->GetSize ()) {
		const SubCheckState& sub = (*subChecksPtr)[subIdx];
		ApplyAuditResultListHeaders (dialogId, sub.columns);
		for (UIndex i = 0; i < sub.rows.GetSize (); ++i) {
			const AuditRow& r = sub.rows[i];
			(void) DGListInsertItem (dialogId, ResultList, DG_LIST_BOTTOM);
			const short rowPos = DGListGetItemCount (dialogId, ResultList);
			if (rowPos <= 0) {
				continue;
			}
			for (UIndex c = 0; c < r.cells.GetSize () && c < sub.columns.GetSize (); ++c) {
				DGListSetTabItemText (dialogId, ResultList, rowPos, static_cast<short> (c + 1), r.cells[c]);
			}
			DGListSetItemUserData (dialogId, ResultList, rowPos, static_cast<DGUserData> (i + 1));
		}
		if (gPaletteState.selectedCategory == CategoryId::Elements ||
			gPaletteState.selectedCategory == CategoryId::Docs) {
			ChlupApplyListRowHighlighting (dialogId);
		}
		return;
	}

	GS::Array<AuditColumnDef> emptyCols;
	ApplyAuditResultListHeaders (dialogId, emptyCols);
}

static API_ElemFilterFlags GetFilterBitsForScope (bool activeViewOnly, API_ElemTypeID typeId)
{
	if (!activeViewOnly) {
		return APIFilt_None;
	}
	API_ElemFilterFlags flags = APIFilt_OnVisLayer | APIFilt_HasAccessRight | APIFilt_IsVisibleByRenovation |
		APIFilt_IsInStructureDisplay | APIFilt_IsVisibleByDesignOption;
	if (typeId == API_LineID || typeId == API_PolyLineID) {
		flags |= APIFilt_OnActFloor;
	}
	return flags;
}

static GS::UniString FormatFailReason (std::u16string_view dimLabel, bool dimFail, bool angleFail, CheckMode mode)
{
	if (mode == CheckMode::Modular || !angleFail) {
		return GS::UniString (dimLabel);
	}
	if (!dimFail && angleFail) {
		return GS::UniString (u"Úhel");
	}
	return GS::UniString (dimLabel) + GS::UniString (u" + Úhel");
}

static void CheckAndAppendLinearDimension (
	const ScanResult&		baseInfo,
	std::u16string_view	dimLabelUtf8,
	double				dimMm,
	double				angleDeg,
	bool				hasAngle,
	CheckMode			mode,
	double				moduleMm,
	double				tolLength,
	double				tolAngle)
{
	double	devLen		= 0.0;
	double	devAng		= 0.0;
	bool	lenFail		= false;
	bool	angleFail	= false;

	if (mode == CheckMode::Hairy) {
		lenFail		= HairyLengthFails (dimMm, tolLength, devLen);
		angleFail	= hasAngle && IsAngleOut (angleDeg, tolAngle, devAng);
	} else {
		lenFail		= ModularLengthFails (dimMm, moduleMm);
	}

	if (lenFail || angleFail) {
		ScanResult res			= baseInfo;
		res.checkedParam		= FormatFailReason (dimLabelUtf8, lenFail, angleFail, mode);
		res.lengthMm			= dimMm;
		res.lengthDeviationMm	= devLen;
		res.angleDeg			= angleDeg;
		res.angleDeviationDeg	= devAng;
		res.hasAngleValue		= hasAngle;
		gPaletteState.results.Push (res);
	}
}

static void CollectTypeResults (
	API_ElemTypeID									typeId,
	bool											activeViewOnly,
	bool											selectedOnly,
	const GS::HashSet<API_Guid>&					selectedGuids,
	const GS::HashTable<short, GS::UniString>&		storyMap,
	CheckMode										mode,
	double											moduleMm,
	double											tolLength,
	double											tolAngle)
{
	GS::Array<API_Guid> guids;
	if (ACAPI_Element_GetElemList (typeId, &guids, GetFilterBitsForScope (activeViewOnly, typeId)) != NoError) {
		return;
	}

	for (const API_Guid& guid : guids) {
		if (selectedOnly && !selectedGuids.Contains (guid)) {
			continue;
		}
		API_Element element;
		BNZeroMemory (&element, sizeof (API_Element));
		element.header.guid = guid;
		if (ACAPI_Element_Get (&element) != NoError) {
			continue;
		}

		ScanResult baseInfo;
		baseInfo.guid			= guid;
		baseInfo.type			= typeId;
		baseInfo.is3DCapable	= Is3DElementType (typeId);
		baseInfo.homeStory		= LookupHomeStoryName (element.header.floorInd, storyMap);
		GetLayerNameAndHidden (element.header.layer, baseInfo.layerName, baseInfo.layerHidden);

		switch (typeId) {
			case API_WallID: {
				const double lenMm		= Distance2D (element.wall.begC, element.wall.endC) * 1000.0;
				const double angDeg		= CalculateAngleDeg (element.wall.begC, element.wall.endC);
				const double heightMm	= element.wall.height * 1000.0;
				CheckAndAppendLinearDimension (baseInfo, u"Délka", lenMm, angDeg, true, mode, moduleMm, tolLength, tolAngle);
				CheckAndAppendLinearDimension (baseInfo, u"Výška", heightMm, 0.0, false, mode, moduleMm, tolLength, tolAngle);
				break;
			}
			case API_BeamID: {
				const double lenMm		= Distance2D (element.beam.begC, element.beam.endC) * 1000.0;
				const double angDeg		= CalculateAngleDeg (element.beam.begC, element.beam.endC);
				CheckAndAppendLinearDimension (baseInfo, u"Délka", lenMm, angDeg, true, mode, moduleMm, tolLength, tolAngle);
				if (element.header.hasMemo && element.beam.nSegments > 0) {
					API_ElementMemo memo = {};
					if (ACAPI_Element_GetMemo (guid, &memo, APIMemoMask_BeamSegment) == NoError && memo.beamSegments != nullptr) {
						const double heightMm = memo.beamSegments[0].assemblySegmentData.nominalHeight * 1000.0;
						CheckAndAppendLinearDimension (baseInfo, u"Výška", heightMm, 0.0, false, mode, moduleMm, tolLength, tolAngle);
					}
					ACAPI_DisposeElemMemoHdls (&memo);
				}
				break;
			}
			case API_ColumnID: {
				const double heightMm	= element.column.height * 1000.0;
				const double angDeg		= NormalizeAngle180 (element.column.axisRotationAngle * 180.0 / kPi);
				CheckAndAppendLinearDimension (baseInfo, u"Výška", heightMm, angDeg, true, mode, moduleMm, tolLength, tolAngle);
				break;
			}
			case API_LineID: {
				const double lenMm		= Distance2D (element.line.begC, element.line.endC) * 1000.0;
				const double angDeg		= CalculateAngleDeg (element.line.begC, element.line.endC);
				CheckAndAppendLinearDimension (baseInfo, u"Délka", lenMm, angDeg, true, mode, moduleMm, tolLength, tolAngle);
				break;
			}
			case API_PolyLineID: {
				if (!element.header.hasMemo) {
					break;
				}
				API_ElementMemo memo = {};
				if (ACAPI_Element_GetMemo (guid, &memo, APIMemoMask_Polygon) != NoError || memo.coords == nullptr) {
					ACAPI_DisposeElemMemoHdls (&memo);
					break;
				}
				for (Int32 seg = 1; seg < element.polyLine.poly.nCoords; ++seg) {
					const double segLenMm	= Distance2D ((*memo.coords)[seg], (*memo.coords)[seg + 1]) * 1000.0;
					const double segAngDeg	= CalculateAngleDeg ((*memo.coords)[seg], (*memo.coords)[seg + 1]);

					double	devLen		= 0.0;
					double	devAng		= 0.0;
					bool	lenFail		= false;
					bool	angleFail	= false;

					if (mode == CheckMode::Hairy) {
						lenFail		= HairyLengthFails (segLenMm, tolLength, devLen);
						angleFail	= IsAngleOut (segAngDeg, tolAngle, devAng);
					} else {
						lenFail		= ModularLengthFails (segLenMm, moduleMm);
					}

					if (lenFail || angleFail) {
						ScanResult res			= baseInfo;
						const GS::UniString segStr = GS::UniString (u"Segment č. ") + GS::ValueToUniString (static_cast<int> (seg));
						if (mode == CheckMode::Modular) {
							res.checkedParam = segStr;
						} else if (lenFail && angleFail) {
							res.checkedParam = segStr + GS::UniString (u" (Délka + Úhel)");
						} else if (lenFail) {
							res.checkedParam = segStr + GS::UniString (u" (Délka)");
						} else {
							res.checkedParam = segStr + GS::UniString (u" (Úhel)");
						}
						res.lengthMm			= segLenMm;
						res.lengthDeviationMm	= devLen;
						res.angleDeg			= segAngDeg;
						res.angleDeviationDeg	= devAng;
						res.hasAngleValue		= true;
						gPaletteState.results.Push (res);
					}
				}
				ACAPI_DisposeElemMemoHdls (&memo);
				break;
			}
			default:
				break;
		}
	}
}

static bool GetElementGuidForCurrentListRow (UIndex listIndex, API_Guid& outGuid, bool& outIs3DCapable)
{
	outGuid        = APINULLGuid;
	outIs3DCapable = false;
	if (listIndex == 0) {
		return false;
	}
	if (gPaletteState.selectedCategory == CategoryId::Accuracy) {
		if (listIndex > gPaletteState.results.GetSize ()) {
			return false;
		}
		const ScanResult& r = gPaletteState.results[listIndex - 1];
		outGuid        = r.guid;
		outIs3DCapable = r.is3DCapable;
		return (outGuid != APINULLGuid);
	}
	if ((gPaletteState.selectedCategory == CategoryId::Elements ||
		 gPaletteState.selectedCategory == CategoryId::Docs) &&
		gPaletteState.selectedSubIdx >= 0) {
		const GS::Array<SubCheckState>* subChecksPtr = GetSubChecksForCategory (gPaletteState.selectedCategory);
		if (subChecksPtr != nullptr && static_cast<UIndex> (gPaletteState.selectedSubIdx) < subChecksPtr->GetSize ()) {
			const SubCheckState& sub = (*subChecksPtr)[gPaletteState.selectedSubIdx];
			if (listIndex > sub.rows.GetSize ()) {
				return false;
			}
			const AuditRow& r = sub.rows[listIndex - 1];
			outGuid        = r.elemGuid;
			outIs3DCapable = r.is3DCapable;
			return (outGuid != APINULLGuid);
		}
	}
	return false;
}

static void SelectResultOnly (UIndex index, bool in3D)
{
	API_Guid guid  = APINULLGuid;
	bool     can3D = false;
	if (!GetElementGuidForCurrentListRow (index, guid, can3D)) {
		return;
	}
	ACAPI_Selection_DeselectAll ();
	ACAPI_Selection_Select ({ API_Neig (guid) }, true);
	if (in3D && can3D) {
		ACAPI_View_ShowSelectionIn3D ();
	}
}

static void SelectResultAndZoom (UIndex index, bool openIn3D)
{
	API_Guid guid  = APINULLGuid;
	bool     can3D = false;
	if (!GetElementGuidForCurrentListRow (index, guid, can3D)) {
		return;
	}
	ACAPI_Selection_DeselectAll ();
	ACAPI_Selection_Select ({ API_Neig (guid) }, true);
	if (openIn3D && can3D) {
		ACAPI_View_ShowSelectionIn3D ();
		return;
	}
	GS::Array<API_Guid> toZoom;
	toZoom.Push (guid);
	if (ACAPI_View_ZoomToElements (&toZoom) != NoError) {
		ACAPI_View_ZoomToSelected ();
	}
}

static void ShowListContextMenu (short dialogId, UIndex listIndex, const DG::ContextMenuEventArg* ev)
{
	(void) dialogId;
	API_Guid guid  = APINULLGuid;
	bool     can3D = false;
	if (ev == nullptr || !GetElementGuidForCurrentListRow (listIndex, guid, can3D)) {
		return;
	}
	DG::Command			cSel2D (CmdSelect2D, kCegraDev, kCegraProduct);
	DG::Command			cSel3D (CmdSelect3D, kCegraDev, kCegraProduct);
	DG::Command			cZ2 (CmdSelectZoom2D, kCegraDev, kCegraProduct);
	DG::Command			cZ3 (CmdSelectZoom3D, kCegraDev, kCegraProduct);
	DG::Menu			menu ("cegra-list");
	if (can3D) {
		menu.AddMenuItem (DG::MenuSimpleItem (cSel2D));
		menu.AddMenuItem (DG::MenuSimpleItem (cSel3D));
		menu.AddMenuItem (DG::MenuSimpleItem (cZ2));
		menu.AddMenuItem (DG::MenuSimpleItem (cZ3));
	} else {
		menu.AddMenuItem (DG::MenuSimpleItem (cSel2D));
		menu.AddMenuItem (DG::MenuSimpleItem (cZ2));
	}
	DG::CommandTable table;
	if (can3D) {
		table.Add (cSel2D, new DG::CommandDescriptor (cSel2D, GS::UniString (u"Vybrat ve 2D")));
		table.Add (cSel3D, new DG::CommandDescriptor (cSel3D, GS::UniString (u"Vybrat ve 3D")));
		table.Add (cZ2, new DG::CommandDescriptor (cZ2, GS::UniString (u"Vybrat a přiblížit ve 2D")));
		table.Add (cZ3, new DG::CommandDescriptor (cZ3, GS::UniString (u"Vybrat a přiblížit ve 3D")));
	} else {
		table.Add (cSel2D, new DG::CommandDescriptor (cSel2D, GS::UniString (u"Vybrat ve 2D")));
		table.Add (cZ2, new DG::CommandDescriptor (cZ2, GS::UniString (u"Vybrat a přiblížit ve 2D")));
	}
	DG::ContextMenu ctx (GS::UniString ("cegra"), &menu);
	ctx.SetEnabledCommands (table);
	const DG::NativePoint pos = DG::NativePoint (ev->xPos, ev->yPos);
	DG::CommandEvent* cmdEv = ctx.Display (pos);
	if (cmdEv != nullptr) {
		const ULong id = cmdEv->GetCommand ().GetCommandId ();
		delete cmdEv;
		if (id == CmdSelect2D) {
			SelectResultOnly (listIndex, false);
		} else if (id == CmdSelect3D) {
			SelectResultOnly (listIndex, true);
		} else if (id == CmdSelectZoom2D) {
			SelectResultAndZoom (listIndex, false);
		} else if (id == CmdSelectZoom3D) {
			SelectResultAndZoom (listIndex, true);
		}
	}
	for (auto it = table.EnumerateValues (); it != nullptr; ++it) {
		delete *it;
	}
}

static void SyncDimensionNotePenOverrides (const GS::Array<DimNotePenTarget>& targets)
{
	if (gDimPenBackup.IsEmpty () && targets.IsEmpty ()) {
		return;
	}

	const short targetPen = std::clamp<short> (gDimHighlightPen, 1, 255);
	if (gDimPenBackup.GetSize () == targets.GetSize ()) {
		bool same = true;
		for (UIndex i = 0; i < targets.GetSize (); ++i) {
			if (gDimPenBackup[i].elemGuid != targets[i].elemGuid ||
				gDimPenBackup[i].subElemIndex != targets[i].subElemIndex ||
				gDimPenBackup[i].appliedHighlightPen != targetPen) {
				same = false;
				break;
			}
		}
		if (same) {
			return;
		}
	}

	gIsSyncingDimPens = true;

	/* Seskupíme všechny dotčené kótovací prvky (ze zálohy i nových cílů), aby se každý prvek
	 * v databázi měnil přes ACAPI_Element_Change maximálně jednou v rámci transakce. */
	GS::Array<API_Guid>		affectedGuids;
	GS::HashSet<API_Guid>	seenGuids;
	for (UIndex i = 0; i < gDimPenBackup.GetSize (); ++i) {
		const API_Guid& g = gDimPenBackup[i].elemGuid;
		if (g != APINULLGuid && !seenGuids.Contains (g)) {
			seenGuids.Add (g);
			affectedGuids.Push (g);
		}
	}
	for (UIndex i = 0; i < targets.GetSize (); ++i) {
		const API_Guid& g = targets[i].elemGuid;
		if (g != APINULLGuid && !seenGuids.Contains (g)) {
			seenGuids.Add (g);
			affectedGuids.Push (g);
		}
	}

	const GS::Array<DimNotePenBackupEntry> oldBackup = gDimPenBackup;
	GS::Array<DimNotePenBackupEntry>       newBackup;

	const GS::UniString undoText (u"Zvýraznění přepsaných kót");
	const GSErrCode cmdErr = ACAPI_CallUndoableCommand (undoText, [&] () -> GSErrCode {
		for (UIndex gIdx = 0; gIdx < affectedGuids.GetSize (); ++gIdx) {
			const API_Guid elemGuid = affectedGuids[gIdx];
			API_Element elem;
			BNZeroMemory (&elem, sizeof (API_Element));
			elem.header.guid = elemGuid;
			if (ACAPI_Element_Get (&elem) != NoError) {
				continue;
			}

			switch (elem.header.type.typeID) {
				case API_DimensionID: {
					API_ElementMemo memo = {};
					if (ACAPI_Element_GetMemo (elemGuid, &memo, APIMemoMask_All) == NoError && memo.dimElems != nullptr) {
						const Int32 nDim = BMGetHandleSize (reinterpret_cast<GSHandle> (memo.dimElems)) /
										   static_cast<Int32> (sizeof (API_DimElem));
						bool changed = false;

						/* 1. Obnovení původních per ze zálohy v paměti */
						for (UIndex b = 0; b < oldBackup.GetSize (); ++b) {
							const DimNotePenBackupEntry& entry = oldBackup[b];
							if (entry.elemGuid != elemGuid) {
								continue;
							}
							if (entry.subElemIndex >= 0 && entry.subElemIndex < nDim) {
								if ((*memo.dimElems)[entry.subElemIndex].note.notePen == entry.appliedHighlightPen) {
									(*memo.dimElems)[entry.subElemIndex].note.notePen = entry.originalNotePen;
									changed = true;
								}
							}
						}

						/* 2. Aplikace zvýrazňovacího pera pro požadované úseky v paměti */
						GS::Array<DimNotePenBackupEntry> elemNewBackups;
						for (UIndex tIdx = 0; tIdx < targets.GetSize (); ++tIdx) {
							const DimNotePenTarget& t = targets[tIdx];
							if (t.elemGuid != elemGuid) {
								continue;
							}
							if (t.subElemIndex >= 0 && t.subElemIndex < nDim) {
								const short origPen = (*memo.dimElems)[t.subElemIndex].note.notePen;
								(*memo.dimElems)[t.subElemIndex].note.notePen = targetPen;
								elemNewBackups.Push ({ elemGuid, t.subElemIndex, origPen, targetPen });
								changed = true;
							}
						}

						if (changed) {
							API_Element mask;
							ACAPI_ELEMENT_MASK_CLEAR (mask);
							if (ACAPI_Element_Change (&elem, &mask, &memo, 0, true) == NoError) {
								for (const DimNotePenBackupEntry& nb : elemNewBackups) {
									newBackup.Push (nb);
								}
							}
						}
					}
					ACAPI_DisposeElemMemoHdls (&memo);
					break;
				}
				case API_RadialDimensionID: {
					bool changed = false;
					for (UIndex b = 0; b < oldBackup.GetSize (); ++b) {
						const DimNotePenBackupEntry& entry = oldBackup[b];
						if (entry.elemGuid == elemGuid && elem.radialDimension.note.notePen == entry.appliedHighlightPen) {
							elem.radialDimension.note.notePen = entry.originalNotePen;
							changed = true;
						}
					}
					GS::Array<DimNotePenBackupEntry> elemNewBackups;
					for (UIndex tIdx = 0; tIdx < targets.GetSize (); ++tIdx) {
						const DimNotePenTarget& t = targets[tIdx];
						if (t.elemGuid == elemGuid) {
							const short origPen = elem.radialDimension.note.notePen;
							elem.radialDimension.note.notePen = targetPen;
							elemNewBackups.Push ({ elemGuid, t.subElemIndex, origPen, targetPen });
							changed = true;
						}
					}
					if (changed) {
						API_Element mask;
						ACAPI_ELEMENT_MASK_CLEAR (mask);
						ACAPI_ELEMENT_MASK_SET (mask, API_RadialDimensionType, note.notePen);
						if (ACAPI_Element_Change (&elem, &mask, nullptr, 0, true) == NoError) {
							for (const DimNotePenBackupEntry& nb : elemNewBackups) {
								newBackup.Push (nb);
							}
						}
					}
					break;
				}
				case API_LevelDimensionID: {
					bool changed1 = false;
					bool changed2 = false;
					for (UIndex b = 0; b < oldBackup.GetSize (); ++b) {
						const DimNotePenBackupEntry& entry = oldBackup[b];
						if (entry.elemGuid != elemGuid) {
							continue;
						}
						if (entry.subElemIndex == 1 && elem.levelDimension.note1.notePen == entry.appliedHighlightPen) {
							elem.levelDimension.note1.notePen = entry.originalNotePen;
							changed1 = true;
						} else if (entry.subElemIndex == 2 && elem.levelDimension.note2.notePen == entry.appliedHighlightPen) {
							elem.levelDimension.note2.notePen = entry.originalNotePen;
							changed2 = true;
						}
					}
					GS::Array<DimNotePenBackupEntry> elemNewBackups;
					for (UIndex tIdx = 0; tIdx < targets.GetSize (); ++tIdx) {
						const DimNotePenTarget& t = targets[tIdx];
						if (t.elemGuid != elemGuid) {
							continue;
						}
						if (t.subElemIndex == 1) {
							const short origPen = elem.levelDimension.note1.notePen;
							elem.levelDimension.note1.notePen = targetPen;
							elemNewBackups.Push ({ elemGuid, 1, origPen, targetPen });
							changed1 = true;
						} else if (t.subElemIndex == 2) {
							const short origPen = elem.levelDimension.note2.notePen;
							elem.levelDimension.note2.notePen = targetPen;
							elemNewBackups.Push ({ elemGuid, 2, origPen, targetPen });
							changed2 = true;
						}
					}
					if (changed1 || changed2) {
						API_Element mask;
						ACAPI_ELEMENT_MASK_CLEAR (mask);
						if (changed1) {
							ACAPI_ELEMENT_MASK_SET (mask, API_LevelDimensionType, note1.notePen);
						}
						if (changed2) {
							ACAPI_ELEMENT_MASK_SET (mask, API_LevelDimensionType, note2.notePen);
						}
						if (ACAPI_Element_Change (&elem, &mask, nullptr, 0, true) == NoError) {
							for (const DimNotePenBackupEntry& nb : elemNewBackups) {
								newBackup.Push (nb);
							}
						}
					}
					break;
				}
				case API_AngleDimensionID: {
					bool changed = false;
					for (UIndex b = 0; b < oldBackup.GetSize (); ++b) {
						const DimNotePenBackupEntry& entry = oldBackup[b];
						if (entry.elemGuid == elemGuid && elem.angleDimension.note.notePen == entry.appliedHighlightPen) {
							elem.angleDimension.note.notePen = entry.originalNotePen;
							changed = true;
						}
					}
					GS::Array<DimNotePenBackupEntry> elemNewBackups;
					for (UIndex tIdx = 0; tIdx < targets.GetSize (); ++tIdx) {
						const DimNotePenTarget& t = targets[tIdx];
						if (t.elemGuid == elemGuid) {
							const short origPen = elem.angleDimension.note.notePen;
							elem.angleDimension.note.notePen = targetPen;
							elemNewBackups.Push ({ elemGuid, t.subElemIndex, origPen, targetPen });
							changed = true;
						}
					}
					if (changed) {
						API_Element mask;
						ACAPI_ELEMENT_MASK_CLEAR (mask);
						ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.notePen);
						if (ACAPI_Element_Change (&elem, &mask, nullptr, 0, true) == NoError) {
							for (const DimNotePenBackupEntry& nb : elemNewBackups) {
								newBackup.Push (nb);
							}
						}
					}
					break;
				}
				default:
					break;
			}
		}
		return NoError;
	});

	if (cmdErr == NoError) {
		gDimPenBackup = newBackup;
	}
	gIsSyncingDimPens = false;

	ACAPI_WriteReport ("Kontrola modelu (Kóty): aktivních přebarvených textů kót = %d (pero %d, err = %d).",
		false, static_cast<int> (gDimPenBackup.GetSize ()), static_cast<int> (targetPen), static_cast<int> (cmdErr));
}

static void RestoreDimensionNotePenOverrides (void)
{
	if (gDimPenBackup.IsEmpty ()) {
		return;
	}
	GS::Array<DimNotePenTarget> emptyTargets;
	SyncDimensionNotePenOverrides (emptyTargets);
}

static API_AttributeIndex GetSafeVisibleUnlockedLayer (API_AttributeIndex preferredLayer)
{
	if (preferredLayer != APIInvalidAttributeIndex && preferredLayer != APIApplicationLayerAttributeIndex) {
		API_Attribute layerAttr;
		BNZeroMemory (&layerAttr, sizeof (API_Attribute));
		layerAttr.header.typeID = API_LayerID;
		layerAttr.header.index  = preferredLayer;
		if (ACAPI_Attribute_Get (&layerAttr) == NoError) {
			if ((layerAttr.layer.head.flags & APILay_Hidden) == 0 &&
				(layerAttr.layer.head.flags & APILay_Locked) == 0) {
				return preferredLayer;
			}
		}
	}
	return APIApplicationLayerAttributeIndex;
}

static void SyncStaticDimensionCircles (const GS::Array<StaticDimCircleTarget>& targets)
{
	if (gStaticDimCircles.IsEmpty () && targets.IsEmpty ()) {
		return;
	}

	const short  targetPen    = std::clamp<short> (gDimHighlightPen, 1, 255);
	const double targetDiamMm = std::clamp (gStaticDimCircleDiamMm, 1.0, 50000.0);

	if (gStaticDimCircles.GetSize () == targets.GetSize ()) {
		bool same = true;
		for (UIndex i = 0; i < targets.GetSize (); ++i) {
			const StaticDimCircleEntry& e = gStaticDimCircles[i];
			const StaticDimCircleTarget& t = targets[i];
			if (e.parentDimGuid != t.parentDimGuid ||
				e.subElemIndex != t.subElemIndex ||
				e.appliedPen != targetPen ||
				std::fabs (e.appliedDiamMm - targetDiamMm) > 1e-3 ||
				std::fabs (e.centerPos.x - t.centerPos.x) > 1e-5 ||
				std::fabs (e.centerPos.y - t.centerPos.y) > 1e-5) {
				same = false;
				break;
			}
			API_Elem_Head cHead;
			BNZeroMemory (&cHead, sizeof (API_Elem_Head));
#if defined (ServerMainVers_3000)
			cHead.guid = e.usedNativeWitnessPen ? e.parentDimGuid : e.circleGuid;
#else
			cHead.guid = e.circleGuid;
#endif
			if (ACAPI_Element_GetHeader (&cHead) != NoError) {
				same = false;
				break;
			}
		}
		if (same) {
			return;
		}
	}

	gIsSyncingDimPens = true;

	const GS::Array<StaticDimCircleEntry> oldCircles = gStaticDimCircles;
	GS::Array<StaticDimCircleEntry>       newCircles;

	const GS::UniString undoText (u"Zvýraznění statických částí kót");
	const GSErrCode cmdErr = ACAPI_CallUndoableCommand (undoText, [&] () -> GSErrCode {
		if (!oldCircles.IsEmpty ()) {
			GS::Array<API_Guid> toDelete;
			for (const StaticDimCircleEntry& e : oldCircles) {
				if (e.circleGuid != APINULLGuid) {
					API_Elem_Head cHead;
					BNZeroMemory (&cHead, sizeof (API_Elem_Head));
					cHead.guid = e.circleGuid;
					if (ACAPI_Element_GetHeader (&cHead) == NoError) {
						toDelete.Push (e.circleGuid);
					}
				}
			}
			if (!toDelete.IsEmpty ()) {
				(void) ACAPI_Element_Delete (toDelete);
			}

#if defined (ServerMainVers_3000)
			/* V Archicadu 30 obnovíme původní nativní přepsání pera hraničících čar u lineárních kót */
			GS::HashSet<API_Guid> restoredDimGuids;
			for (UIndex i = 0; i < oldCircles.GetSize (); ++i) {
				const StaticDimCircleEntry& e = oldCircles[i];
				if (!e.usedNativeWitnessPen || e.parentDimGuid == APINULLGuid || restoredDimGuids.Contains (e.parentDimGuid)) {
					continue;
				}
				restoredDimGuids.Add (e.parentDimGuid);

				API_Element dimElem;
				BNZeroMemory (&dimElem, sizeof (API_Element));
				dimElem.header.guid = e.parentDimGuid;
				if (ACAPI_Element_Get (&dimElem) != NoError || dimElem.header.type.typeID != API_DimensionID) {
					continue;
				}
				API_ElementMemo memo = {};
				if (ACAPI_Element_GetMemo (e.parentDimGuid, &memo, APIMemoMask_All) == NoError && memo.dimElems != nullptr) {
					const Int32 nDim = BMGetHandleSize (reinterpret_cast<GSHandle> (memo.dimElems)) /
									   static_cast<Int32> (sizeof (API_DimElem));
					bool changed = false;
					for (UIndex j = 0; j < oldCircles.GetSize (); ++j) {
						const StaticDimCircleEntry& ej = oldCircles[j];
						if (!ej.usedNativeWitnessPen || ej.parentDimGuid != e.parentDimGuid) {
							continue;
						}
						if (ej.subElemIndex >= 0 && ej.subElemIndex < nDim) {
							API_DimElem& de = (*memo.dimElems)[ej.subElemIndex];
							if (de.witnessExtensionLinePen == ej.appliedPen) {
								de.witnessOverrideExtension = ej.origWitnessOverride;
								de.witnessExtensionLinePen  = ej.origWitnessExtPen;
								de.witnessForm              = ej.origWitnessForm;
								changed = true;
							}
						}
					}
					if (changed) {
						API_Element mask;
						ACAPI_ELEMENT_MASK_CLEAR (mask);
						(void) ACAPI_Element_Change (&dimElem, &mask, &memo, 0, true);
					}
				}
				ACAPI_DisposeElemMemoHdls (&memo);
			}
#endif
		}

		if (!targets.IsEmpty ()) {
#if defined (ServerMainVers_3000)
			/* V Archicadu 30 přebarvíme statické hraničící čáry lineárních kót nativně přímo v API_DimElem */
			GS::HashSet<API_Guid> processedNativeDimGuids;
			for (UIndex i = 0; i < targets.GetSize (); ++i) {
				const API_Guid dimGuid = targets[i].parentDimGuid;
				if (dimGuid == APINULLGuid || processedNativeDimGuids.Contains (dimGuid)) {
					continue;
				}
				API_Element dimElem;
				BNZeroMemory (&dimElem, sizeof (API_Element));
				dimElem.header.guid = dimGuid;
				if (ACAPI_Element_Get (&dimElem) != NoError || dimElem.header.type.typeID != API_DimensionID) {
					continue;
				}
				processedNativeDimGuids.Add (dimGuid);

				API_ElementMemo memo = {};
				if (ACAPI_Element_GetMemo (dimGuid, &memo, APIMemoMask_All) == NoError && memo.dimElems != nullptr) {
					const Int32 nDim = BMGetHandleSize (reinterpret_cast<GSHandle> (memo.dimElems)) /
									   static_cast<Int32> (sizeof (API_DimElem));
					bool changed = false;
					GS::Array<StaticDimCircleEntry> elemNativeEntries;
					for (UIndex j = 0; j < targets.GetSize (); ++j) {
						const StaticDimCircleTarget& tj = targets[j];
						if (tj.parentDimGuid != dimGuid) {
							continue;
						}
						if (tj.subElemIndex >= 0 && tj.subElemIndex < nDim) {
							API_DimElem& de = (*memo.dimElems)[tj.subElemIndex];
							StaticDimCircleEntry ne;
							ne.circleGuid           = APINULLGuid;
							ne.parentDimGuid        = dimGuid;
							ne.subElemIndex         = tj.subElemIndex;
							ne.centerPos            = tj.centerPos;
							ne.appliedPen           = targetPen;
							ne.appliedDiamMm        = targetDiamMm;
							ne.usedNativeWitnessPen = true;
							ne.origWitnessOverride  = de.witnessOverrideExtension;
							ne.origWitnessExtPen    = de.witnessExtensionLinePen;
							ne.origWitnessForm      = de.witnessForm;

							de.witnessOverrideExtension = true;
							de.witnessExtensionLinePen  = targetPen;
							if (de.witnessForm != APIWtn_Fix && de.witnessForm != APIWtn_Large) {
								de.witnessForm = APIWtn_Fix;
							}
							elemNativeEntries.Push (ne);
							changed = true;
						}
					}
					if (changed) {
						API_Element mask;
						ACAPI_ELEMENT_MASK_CLEAR (mask);
						if (ACAPI_Element_Change (&dimElem, &mask, &memo, 0, true) == NoError) {
							for (const StaticDimCircleEntry& ne : elemNativeEntries) {
								newCircles.Push (ne);
							}
						}
					}
				}
				ACAPI_DisposeElemMemoHdls (&memo);
			}
#endif

			API_Element baseCircle;
			BNZeroMemory (&baseCircle, sizeof (API_Element));
			baseCircle.header.type = API_ElemType (API_CircleID);
			const bool hasDef = (ACAPI_Element_GetDefaults (&baseCircle, nullptr) == NoError);

			const double radiusM = (targetDiamMm * 0.5) / 1000.0;
			for (const StaticDimCircleTarget& t : targets) {
				if (t.parentDimGuid == APINULLGuid) {
					continue;
				}
#if defined (ServerMainVers_3000)
				if (processedNativeDimGuids.Contains (t.parentDimGuid)) {
					continue;
				}
#endif
				API_Elem_Head dimHead;
				BNZeroMemory (&dimHead, sizeof (API_Elem_Head));
				dimHead.guid = t.parentDimGuid;
				if (ACAPI_Element_GetHeader (&dimHead) != NoError) {
					continue;
				}

				API_Element circleElem;
				if (hasDef) {
					circleElem = baseCircle;
				} else {
					BNZeroMemory (&circleElem, sizeof (API_Element));
				}
				circleElem.header.type                       = API_ElemType (API_CircleID);
				circleElem.header.guid                       = APINULLGuid;
				circleElem.header.floorInd                   = dimHead.floorInd;
				circleElem.header.layer                      = GetSafeVisibleUnlockedLayer (dimHead.layer);
				circleElem.circle.origC                      = t.centerPos;
				circleElem.circle.r                          = radiusM;
				circleElem.circle.angle                      = 0.0;
				circleElem.circle.ratio                      = 1.0;
				circleElem.circle.ltypeInd                   = APISolidLineAttributeIndex;
				circleElem.circle.linePen.penIndex           = targetPen;
				circleElem.circle.linePen.colorOverridePenIndex = 0;
				circleElem.circle.determination              = APILine_DetOrigin;

				if (ACAPI_Element_Create (&circleElem, nullptr) == NoError &&
					circleElem.header.guid != APINULLGuid) {
					StaticDimCircleEntry ce;
					ce.circleGuid    = circleElem.header.guid;
					ce.parentDimGuid = t.parentDimGuid;
					ce.subElemIndex  = t.subElemIndex;
					ce.centerPos     = t.centerPos;
					ce.appliedPen    = targetPen;
					ce.appliedDiamMm = targetDiamMm;
					newCircles.Push (ce);
				}
			}
		}
		return NoError;
	});

	if (cmdErr == NoError) {
		gStaticDimCircles = newCircles;
	}
	gIsSyncingDimPens = false;

	ACAPI_WriteReport ("Kontrola modelu (Statické kóty): aktivních kružnic = %d (pero %d, průměr %.1f mm, err = %d).",
		false, static_cast<int> (gStaticDimCircles.GetSize ()), static_cast<int> (targetPen), targetDiamMm, static_cast<int> (cmdErr));
}

static void RestoreStaticDimensionCircles (void)
{
	if (gStaticDimCircles.IsEmpty ()) {
		return;
	}
	GS::Array<StaticDimCircleTarget> emptyTargets;
	SyncStaticDimensionCircles (emptyTargets);
}

static void RestoreAllDimensionHighlights (void)
{
	RestoreDimensionNotePenOverrides ();
	RestoreStaticDimensionCircles ();
}

static void CollectHighlightGuidsForCurrentTab (
	GS::Array<API_Guid>&				outHitGuids,
	GS::Array<API_Guid>&				outExcludedDimGuids,
	GS::Array<DimNotePenTarget>&		outDimPenTargets,
	GS::Array<StaticDimCircleTarget>&	outStaticCircleTargets)
{
	outHitGuids.Clear ();
	outExcludedDimGuids.Clear ();
	outDimPenTargets.Clear ();
	outStaticCircleTargets.Clear ();

	GS::HashSet<API_Guid> uniqueHitGuids;
	GS::HashSet<API_Guid> uniqueExcludedGuids;

	if (gPaletteState.selectedCategory == CategoryId::Accuracy) {
		for (const ScanResult& re : gPaletteState.results) {
			if (re.guid != APINULLGuid && !uniqueHitGuids.Contains (re.guid)) {
				uniqueHitGuids.Add (re.guid);
				outHitGuids.Push (re.guid);
			}
		}
		return;
	}

	if (gPaletteState.selectedCategory == CategoryId::Elements) {
		const GS::Array<SubCheckState>* subChecksPtr = GetSubChecksForCategory (gPaletteState.selectedCategory);
		if (subChecksPtr == nullptr) {
			return;
		}
		const short subIdx = gPaletteState.selectedSubIdx;
		if (subIdx >= 0 && static_cast<UIndex> (subIdx) < subChecksPtr->GetSize ()) {
			const SubCheckState& sub = (*subChecksPtr)[subIdx];
			for (const AuditRow& r : sub.rows) {
				if (r.elemGuid != APINULLGuid && !uniqueHitGuids.Contains (r.elemGuid)) {
					uniqueHitGuids.Add (r.elemGuid);
					outHitGuids.Push (r.elemGuid);
				}
			}
		} else {
			for (UIndex i = 0; i < subChecksPtr->GetSize (); ++i) {
				const SubCheckState& sub = (*subChecksPtr)[i];
				if (!sub.checked) {
					continue;
				}
				for (const AuditRow& r : sub.rows) {
					if (r.elemGuid != APINULLGuid && !uniqueHitGuids.Contains (r.elemGuid)) {
						uniqueHitGuids.Add (r.elemGuid);
						outHitGuids.Push (r.elemGuid);
					}
				}
			}
		}
		return;
	}

	if (gPaletteState.selectedCategory == CategoryId::Docs) {
		const GS::Array<SubCheckState>* subChecksPtr = GetSubChecksForCategory (gPaletteState.selectedCategory);
		if (subChecksPtr == nullptr) {
			return;
		}
		const short subIdx       = gPaletteState.selectedSubIdx;
		const short customDimIdx = static_cast<short> (DocSubCheckId::CustomTextDimensions);
		const short staticDimIdx = static_cast<short> (DocSubCheckId::StaticDimensions);

		if (subIdx == customDimIdx && static_cast<UIndex> (subIdx) < subChecksPtr->GetSize ()) {
			const SubCheckState& sub = (*subChecksPtr)[subIdx];
			if (gSelectedCustomDimRowIndex >= 0 &&
				static_cast<UIndex> (gSelectedCustomDimRowIndex) < sub.rows.GetSize ()) {
				/* Vybrán konkrétní řádek v tabulce: přebarvíme pero pouze tohoto kótovacího textu */
				const AuditRow& r = sub.rows[gSelectedCustomDimRowIndex];
				if (r.elemGuid != APINULLGuid) {
					outDimPenTargets.Push ({ r.elemGuid, r.subElemIndex });
					uniqueExcludedGuids.Add (r.elemGuid);
					outExcludedDimGuids.Push (r.elemGuid);
				}
			} else {
				/* Není vybrán konkrétní řádek: přebarvíme pera všech nalezených přepsaných kótovacích textů */
				for (const AuditRow& r : sub.rows) {
					if (r.elemGuid != APINULLGuid) {
						outDimPenTargets.Push ({ r.elemGuid, r.subElemIndex });
						if (!uniqueExcludedGuids.Contains (r.elemGuid)) {
							uniqueExcludedGuids.Add (r.elemGuid);
							outExcludedDimGuids.Push (r.elemGuid);
						}
					}
				}
			}
		} else if (subIdx == staticDimIdx && static_cast<UIndex> (subIdx) < subChecksPtr->GetSize ()) {
			const SubCheckState& sub = (*subChecksPtr)[subIdx];
			if (gSelectedCustomDimRowIndex >= 0 &&
				static_cast<UIndex> (gSelectedCustomDimRowIndex) < sub.rows.GetSize ()) {
				/* Vybrán konkrétní řádek v tabulce: vykreslíme kružnici pouze kolem této statické části kóty */
				const AuditRow& r = sub.rows[gSelectedCustomDimRowIndex];
				if (r.elemGuid != APINULLGuid && r.hasMarkerPos) {
					outStaticCircleTargets.Push ({ r.elemGuid, r.subElemIndex, r.markerPos });
					uniqueExcludedGuids.Add (r.elemGuid);
					outExcludedDimGuids.Push (r.elemGuid);
				}
			} else {
				/* Není vybrán konkrétní řádek: vykreslíme kružnice kolem všech nalezených statických částí kót */
				for (const AuditRow& r : sub.rows) {
					if (r.elemGuid != APINULLGuid && r.hasMarkerPos) {
						outStaticCircleTargets.Push ({ r.elemGuid, r.subElemIndex, r.markerPos });
						if (!uniqueExcludedGuids.Contains (r.elemGuid)) {
							uniqueExcludedGuids.Add (r.elemGuid);
							outExcludedDimGuids.Push (r.elemGuid);
						}
					}
				}
			}
		} else if (subIdx >= 0 && static_cast<UIndex> (subIdx) < subChecksPtr->GetSize ()) {
			const SubCheckState& sub = (*subChecksPtr)[subIdx];
			for (const AuditRow& r : sub.rows) {
				if (r.elemGuid != APINULLGuid && !uniqueHitGuids.Contains (r.elemGuid)) {
					uniqueHitGuids.Add (r.elemGuid);
					outHitGuids.Push (r.elemGuid);
				}
			}
		} else {
			/* Nadřazená kategorie Dokumentace: popisky přes hitGuids, kóty přes pera textů / kružnice + excludedGuids */
			for (UIndex i = 0; i < subChecksPtr->GetSize (); ++i) {
				const SubCheckState& sub = (*subChecksPtr)[i];
				if (!sub.checked) {
					continue;
				}
				if (static_cast<short> (i) == customDimIdx) {
					for (const AuditRow& r : sub.rows) {
						if (r.elemGuid != APINULLGuid) {
							outDimPenTargets.Push ({ r.elemGuid, r.subElemIndex });
							if (!uniqueExcludedGuids.Contains (r.elemGuid)) {
								uniqueExcludedGuids.Add (r.elemGuid);
								outExcludedDimGuids.Push (r.elemGuid);
							}
						}
					}
				} else if (static_cast<short> (i) == staticDimIdx) {
					for (const AuditRow& r : sub.rows) {
						if (r.elemGuid != APINULLGuid && r.hasMarkerPos) {
							outStaticCircleTargets.Push ({ r.elemGuid, r.subElemIndex, r.markerPos });
							if (!uniqueExcludedGuids.Contains (r.elemGuid)) {
								uniqueExcludedGuids.Add (r.elemGuid);
								outExcludedDimGuids.Push (r.elemGuid);
							}
						}
					}
				} else {
					for (const AuditRow& r : sub.rows) {
						if (r.elemGuid != APINULLGuid && !uniqueHitGuids.Contains (r.elemGuid)) {
							uniqueHitGuids.Add (r.elemGuid);
							outHitGuids.Push (r.elemGuid);
						}
					}
				}
			}
		}
	}
}

static void RecolorModelByCurrentTab (short dialogId, bool manualButtonTrigger)
{
	if (!manualButtonTrigger && !gEnableHighlight) {
		RestoreAllDimensionHighlights ();
		(void) CegraVisual_Remove ();
		return;
	}

	GS::Array<API_Guid>					hGuids;
	GS::Array<API_Guid>					excludedDimGuids;
	GS::Array<DimNotePenTarget>			dimPenTargets;
	GS::Array<StaticDimCircleTarget>	staticCircleTargets;
	CollectHighlightGuidsForCurrentTab (hGuids, excludedDimGuids, dimPenTargets, staticCircleTargets);

	SyncDimensionNotePenOverrides (dimPenTargets);
	SyncStaticDimensionCircles (staticCircleTargets);

	for (const StaticDimCircleEntry& cEntry : gStaticDimCircles) {
		if (cEntry.circleGuid != APINULLGuid) {
			excludedDimGuids.Push (cEntry.circleGuid);
		}
	}

	if (hGuids.IsEmpty () && excludedDimGuids.IsEmpty ()) {
		(void) CegraVisual_Remove ();
		if (manualButtonTrigger && dialogId != 0) {
			if (gPaletteState.selectedCategory != CategoryId::Accuracy &&
				gPaletteState.selectedCategory != CategoryId::Elements &&
				gPaletteState.selectedCategory != CategoryId::Docs) {
				SetStatusText (dialogId, GS::UniString (u"Zvolená záložka neobsahuje prvky modelu k přebarvení — zvýraznění modelu bylo zrušeno."));
			} else {
				SetStatusText (dialogId, GS::UniString (u"Ve zvolené záložce nejsou žádné nalezené prvky k přebarvení."));
			}
		}
		return;
	}

	if (!excludedDimGuids.IsEmpty ()) {
		GS::HashSet<API_Guid> excludedSet;
		for (const API_Guid& g : excludedDimGuids) {
			excludedSet.Add (g);
		}
		CegraVisual_ApplyHighlightWithExcluded (hGuids, excludedSet, gHitRgba, gRestRgba);
	} else {
		CegraVisual_ApplyHighlight (hGuids, gHitRgba, gRestRgba);
	}

	if (manualButtonTrigger && dialogId != 0) {
		const int totalHighlighted = static_cast<int> (
			hGuids.GetSize () + dimPenTargets.GetSize () + staticCircleTargets.GetSize ());
		SetStatusText (dialogId,
			GS::UniString (u"Přebarveno ") + GS::ValueToUniString (totalHighlighted) +
			GS::UniString (u" prvků modelu dle zvolené záložky."));
	}
}

static void ApplyCurrentVisualHighlights (void)
{
	if (gPaletteState.dialogId == 0) {
		RestoreAllDimensionHighlights ();
		(void) CegraVisual_Remove ();
		return;
	}
	const bool liveSync = (DGGetItemValLong (gPaletteState.dialogId, ChkLiveSync) != 0);
	if (liveSync || !gDimPenBackup.IsEmpty () || !gStaticDimCircles.IsEmpty ()) {
		RecolorModelByCurrentTab (gPaletteState.dialogId, !liveSync);
	}
}

static bool RunAccuracyScan (short dialogId, bool updateSelectionAndVisuals)
{
	const bool checkWall	= DGGetItemValLong (dialogId, BtnWall) != 0;
	const bool checkBeam	= DGGetItemValLong (dialogId, BtnBeam) != 0;
	const bool checkColumn	= DGGetItemValLong (dialogId, BtnColumn) != 0;
	const bool checkLine	= DGGetItemValLong (dialogId, BtnLine) != 0;
	const bool checkPoly	= DGGetItemValLong (dialogId, BtnPolyLine) != 0;
	if (!checkWall && !checkBeam && !checkColumn && !checkLine && !checkPoly) {
		SetStatusText (dialogId, GS::UniString (u"Vyberte alespoň jeden nástroj ke kontrole přesnosti."));
		return false;
	}
	const bool		selectedOnly	= DGGetItemValLong (dialogId, ChkSelectedOnly) != 0;
	const bool		activeView		= DGGetItemValLong (dialogId, RadActiveView) != 0;
	const bool		hairy			= DGGetItemValLong (dialogId, RadHairy) != 0;
	const CheckMode	mode			= hairy ? CheckMode::Hairy : CheckMode::Modular;
	double			moduleMm		= 0.0;
	double			tolLength		= 0.0;
	double			tolAngle		= 0.0;

	if (hairy) {
		if (!GetDialogTextDouble (dialogId, EditToleranceMm, tolLength, 0.0) ||
			!GetDialogTextDouble (dialogId, EditAngleTolerance, tolAngle, 0.0)) {
			SetStatusText (dialogId, GS::UniString (u"Zadejte platnou toleranci jako číslo (lze použít čárku i tečku)."));
			return false;
		}
		if (tolLength < 0.0 || tolAngle < 0.0) {
			SetStatusText (dialogId, GS::UniString (u"Tolerance musí být nezáporná."));
			return false;
		}
	} else {
		if (!GetDialogTextDouble (dialogId, EditModuleMm, moduleMm, 0.0)) {
			SetStatusText (dialogId, GS::UniString (u"Zadejte modul [mm] jako číslo (lze použít čárku i tečku)."));
			return false;
		}
		if (moduleMm <= 0.0) {
			SetStatusText (dialogId, GS::UniString (u"Zadejte kladný modul [mm]."));
			return false;
		}
	}

	GS::HashSet<API_Guid> selectedGuids;
	if (selectedOnly && GetSelectedGuids (selectedGuids) != NoError) {
		SetStatusText (dialogId, GS::UniString (u"Nepodařilo se načíst aktuální výběr prvků."));
		return false;
	}

	GS::HashTable<short, GS::UniString> storyMap;
	BuildStoryNameMap (storyMap);

	gPaletteState.results.Clear ();

	if (checkWall) {
		CollectTypeResults (API_WallID, activeView, selectedOnly, selectedGuids, storyMap, mode, moduleMm, tolLength, tolAngle);
	}
	if (checkBeam) {
		CollectTypeResults (API_BeamID, activeView, selectedOnly, selectedGuids, storyMap, mode, moduleMm, tolLength, tolAngle);
	}
	if (checkColumn) {
		CollectTypeResults (API_ColumnID, activeView, selectedOnly, selectedGuids, storyMap, mode, moduleMm, tolLength, tolAngle);
	}
	if (checkLine) {
		CollectTypeResults (API_LineID, activeView, selectedOnly, selectedGuids, storyMap, mode, moduleMm, tolLength, tolAngle);
	}
	if (checkPoly) {
		CollectTypeResults (API_PolyLineID, activeView, selectedOnly, selectedGuids, storyMap, mode, moduleMm, tolLength, tolAngle);
	}

	gPaletteState.accuracyHasRun = true;
	gChlupListHighlightGuid = gPaletteState.results.IsEmpty () ? APINULLGuid : gPaletteState.results[0].guid;

	GS::HashSet<API_Guid>	uniqueGuids;
	GS::Array<API_Neig>		highlightNeigs;
	for (const ScanResult& re : gPaletteState.results) {
		if (!uniqueGuids.Contains (re.guid)) {
			uniqueGuids.Add (re.guid);
			highlightNeigs.Push (API_Neig (re.guid));
		}
	}

	if (updateSelectionAndVisuals) {
		ACAPI_Selection_DeselectAll ();
		if (!highlightNeigs.IsEmpty ()) {
			ACAPI_Selection_Select (highlightNeigs, true);
		}
		ApplyCurrentVisualHighlights ();
	}

	ACAPI_WriteReport ("Kontrola modelu (Přesnost): nalezeno %d záznamů (%d unikátních prvků).", false,
		static_cast<int> (gPaletteState.results.GetSize ()),
		static_cast<int> (uniqueGuids.GetSize ()));
	return true;
}

static bool RunAttributesScan (short dialogId, int& outCheckedSubChecks, int& outTotalAttrIssues)
{
	(void) dialogId;
	outCheckedSubChecks = 0;
	outTotalAttrIssues  = 0;

	if (gPaletteState.treeCategories.GetSize () >= 2) {
		const TreeCategoryNode& attrCat = gPaletteState.treeCategories[1];
		for (UIndex i = 0; i < attrCat.children.GetSize () && i < gPaletteState.attrSubChecks.GetSize (); ++i) {
			gPaletteState.attrSubChecks[i].checked = attrCat.children[i].checked;
		}
	}

	for (UIndex i = 0; i < gPaletteState.attrSubChecks.GetSize (); ++i) {
		SubCheckState& sub = gPaletteState.attrSubChecks[i];
		if (!sub.checked) {
			continue;
		}
		++outCheckedSubChecks;
		RunAttributeSubCheck (static_cast<AttributeSubCheckId> (i), gPaletteState.disabledClassSystems, sub);
		outTotalAttrIssues += static_cast<int> (sub.rows.GetSize ());
	}

	if (outCheckedSubChecks == 0) {
		return false;
	}

	ACAPI_WriteReport ("Kontrola modelu (Atributy): provedeno %d kontrol, nalezeno celkem %d problémů.", false,
		outCheckedSubChecks, outTotalAttrIssues);
	return true;
}

static bool RunElementsScan (short dialogId, int& outCheckedSubChecks, int& outTotalElemIssues)
{
	(void) dialogId;
	outCheckedSubChecks = 0;
	outTotalElemIssues  = 0;

	if (gPaletteState.treeCategories.GetSize () >= 4) {
		const TreeCategoryNode& elemCat = gPaletteState.treeCategories[3];
		for (UIndex i = 0; i < elemCat.children.GetSize () && i < gPaletteState.elemSubChecks.GetSize (); ++i) {
			gPaletteState.elemSubChecks[i].checked = elemCat.children[i].checked;
		}
	}

	for (UIndex i = 0; i < gPaletteState.elemSubChecks.GetSize (); ++i) {
		SubCheckState& sub = gPaletteState.elemSubChecks[i];
		if (!sub.checked) {
			continue;
		}
		++outCheckedSubChecks;
		RunElementSubCheck (static_cast<ElementSubCheckId> (i), gPaletteState.disabledClassSystems, sub);
		outTotalElemIssues += static_cast<int> (sub.rows.GetSize ());
	}

	if (outCheckedSubChecks == 0) {
		return false;
	}

	ACAPI_WriteReport ("Kontrola modelu (Prvky modelu): provedeno %d kontrol, nalezeno celkem %d problémů.", false,
		outCheckedSubChecks, outTotalElemIssues);
	return true;
}

static bool RunDefaultsScan (short dialogId, int& outCheckedSubChecks, int& outTotalDefIssues)
{
	(void) dialogId;
	outCheckedSubChecks = 0;
	outTotalDefIssues   = 0;

	if (gPaletteState.treeCategories.GetSize () >= 5) {
		const TreeCategoryNode& defCat = gPaletteState.treeCategories[4];
		for (UIndex i = 0; i < defCat.children.GetSize () && i < gPaletteState.defSubChecks.GetSize (); ++i) {
			gPaletteState.defSubChecks[i].checked = defCat.children[i].checked;
		}
	}

	for (UIndex i = 0; i < gPaletteState.defSubChecks.GetSize (); ++i) {
		SubCheckState& sub = gPaletteState.defSubChecks[i];
		if (!sub.checked) {
			continue;
		}
		++outCheckedSubChecks;
		RunDefaultSubCheck (static_cast<ElementSubCheckId> (i), gPaletteState.disabledClassSystems, sub);
		outTotalDefIssues += static_cast<int> (sub.rows.GetSize ());
	}

	if (outCheckedSubChecks == 0) {
		return false;
	}

	ACAPI_WriteReport ("Kontrola modelu (Výchozí nastavení): provedeno %d kontrol, nalezeno celkem %d problémů.", false,
		outCheckedSubChecks, outTotalDefIssues);
	return true;
}

static bool RunFavoritesScan (short dialogId, int& outCheckedSubChecks, int& outTotalFavIssues)
{
	(void) dialogId;
	outCheckedSubChecks = 0;
	outTotalFavIssues   = 0;

	if (gPaletteState.treeCategories.GetSize () >= 6) {
		const TreeCategoryNode& favCat = gPaletteState.treeCategories[5];
		for (UIndex i = 0; i < favCat.children.GetSize () && i < gPaletteState.favSubChecks.GetSize (); ++i) {
			gPaletteState.favSubChecks[i].checked = favCat.children[i].checked;
		}
	}

	for (UIndex i = 0; i < gPaletteState.favSubChecks.GetSize (); ++i) {
		SubCheckState& sub = gPaletteState.favSubChecks[i];
		if (!sub.checked) {
			continue;
		}
		++outCheckedSubChecks;
		RunFavoriteSubCheck (static_cast<ElementSubCheckId> (i), gPaletteState.disabledClassSystems, sub);
		outTotalFavIssues += static_cast<int> (sub.rows.GetSize ());
	}

	if (outCheckedSubChecks == 0) {
		return false;
	}

	ACAPI_WriteReport ("Kontrola modelu (Oblíbená nastavení): provedeno %d kontrol, nalezeno celkem %d problémů.", false,
		outCheckedSubChecks, outTotalFavIssues);
	return true;
}

static bool RunPropertiesScan (short dialogId, int& outCheckedSubChecks, int& outTotalPropIssues)
{
	(void) dialogId;
	outCheckedSubChecks = 0;
	outTotalPropIssues  = 0;

	if (gPaletteState.treeCategories.GetSize () >= 3) {
		const TreeCategoryNode& propCat = gPaletteState.treeCategories[2];
		for (UIndex i = 0; i < propCat.children.GetSize () && i < gPaletteState.propSubChecks.GetSize (); ++i) {
			gPaletteState.propSubChecks[i].checked = propCat.children[i].checked;
		}
	}

	for (UIndex i = 0; i < gPaletteState.propSubChecks.GetSize (); ++i) {
		SubCheckState& sub = gPaletteState.propSubChecks[i];
		if (!sub.checked) {
			continue;
		}
		++outCheckedSubChecks;
		RunPropertySubCheck (static_cast<PropertySubCheckId> (i), sub);
		outTotalPropIssues += static_cast<int> (sub.rows.GetSize ());
	}

	if (outCheckedSubChecks == 0) {
		return false;
	}

	ACAPI_WriteReport ("Kontrola modelu (Vlastnosti): provedeno %d kontrol, nalezeno celkem %d problémů.", false,
		outCheckedSubChecks, outTotalPropIssues);
	return true;
}

static bool RunViewsScan (short dialogId, int& outCheckedSubChecks, int& outTotalViewIssues)
{
	(void) dialogId;
	outCheckedSubChecks = 0;
	outTotalViewIssues  = 0;

	if (gPaletteState.treeCategories.GetSize () >= 7) {
		const TreeCategoryNode& viewCat = gPaletteState.treeCategories[6];
		for (UIndex i = 0; i < viewCat.children.GetSize () && i < gPaletteState.viewSubChecks.GetSize (); ++i) {
			gPaletteState.viewSubChecks[i].checked = viewCat.children[i].checked;
		}
	}

	for (UIndex i = 0; i < gPaletteState.viewSubChecks.GetSize (); ++i) {
		SubCheckState& sub = gPaletteState.viewSubChecks[i];
		if (!sub.checked) {
			continue;
		}
		++outCheckedSubChecks;
		RunViewSubCheck (static_cast<ViewSubCheckId> (i), sub);
		outTotalViewIssues += static_cast<int> (sub.rows.GetSize ());
	}

	if (outCheckedSubChecks == 0) {
		return false;
	}

	ACAPI_WriteReport ("Kontrola modelu (Zobrazení): provedeno %d kontrol, nalezeno celkem %d problémů.", false,
		outCheckedSubChecks, outTotalViewIssues);
	return true;
}

static bool RunDocsScan (short dialogId, int& outCheckedSubChecks, int& outTotalDocIssues)
{
	(void) dialogId;
	outCheckedSubChecks = 0;
	outTotalDocIssues   = 0;

	if (gPaletteState.treeCategories.GetSize () >= 8) {
		const TreeCategoryNode& docCat = gPaletteState.treeCategories[7];
		for (UIndex i = 0; i < docCat.children.GetSize () && i < gPaletteState.docSubChecks.GetSize (); ++i) {
			gPaletteState.docSubChecks[i].checked = docCat.children[i].checked;
		}
	}

	for (UIndex i = 0; i < gPaletteState.docSubChecks.GetSize (); ++i) {
		SubCheckState& sub = gPaletteState.docSubChecks[i];
		if (!sub.checked) {
			continue;
		}
		++outCheckedSubChecks;
		RunDocSubCheck (static_cast<DocSubCheckId> (i), sub);
		outTotalDocIssues += static_cast<int> (sub.rows.GetSize ());
	}

	if (outCheckedSubChecks == 0) {
		return false;
	}

	ACAPI_WriteReport ("Kontrola modelu (Dokumentace): provedeno %d kontrol, nalezeno celkem %d problémů.", false,
		outCheckedSubChecks, outTotalDocIssues);
	return true;
}

static void RunScan (short dialogId)
{
	bool chkAcc  = false;
	bool chkAttr = false;
	bool chkProp = false;
	bool chkElem = false;
	bool chkDef  = false;
	bool chkFav  = false;
	bool chkView = false;
	bool chkDoc  = false;

	auto hasAnyChecked = [] (const TreeCategoryNode& cat) -> bool {
		for (const TreeChildNode& ch : cat.children) {
			if (ch.checked) {
				return true;
			}
		}
		return false;
	};

	if (gPaletteState.treeCategories.GetSize () >= 8) {
		chkAcc  = hasAnyChecked (gPaletteState.treeCategories[0]);
		chkAttr = hasAnyChecked (gPaletteState.treeCategories[1]);
		chkProp = hasAnyChecked (gPaletteState.treeCategories[2]);
		chkElem = hasAnyChecked (gPaletteState.treeCategories[3]);
		chkDef  = hasAnyChecked (gPaletteState.treeCategories[4]);
		chkFav  = hasAnyChecked (gPaletteState.treeCategories[5]);
		chkView = hasAnyChecked (gPaletteState.treeCategories[6]);
		chkDoc  = hasAnyChecked (gPaletteState.treeCategories[7]);
	}

	gSelectedCustomDimRowIndex = -1;
	RestoreAllDimensionHighlights ();
	(void) CegraVisual_Remove ();

	bool didAny = false;
	int attrCheckedCount = 0, attrTotalIssues = 0;
	int propCheckedCount = 0, propTotalIssues = 0;
	int elemCheckedCount = 0, elemTotalIssues = 0;
	int defCheckedCount  = 0, defTotalIssues  = 0;
	int favCheckedCount  = 0, favTotalIssues  = 0;
	int viewCheckedCount = 0, viewTotalIssues = 0;
	int docCheckedCount  = 0, docTotalIssues  = 0;

	if (chkAcc && RunAccuracyScan (dialogId, gPaletteState.selectedCategory == CategoryId::Accuracy)) {
		didAny = true;
	}
	if (chkAttr && RunAttributesScan (dialogId, attrCheckedCount, attrTotalIssues)) {
		didAny = true;
	}
	if (chkProp && RunPropertiesScan (dialogId, propCheckedCount, propTotalIssues)) {
		didAny = true;
	}
	if (chkElem && RunElementsScan (dialogId, elemCheckedCount, elemTotalIssues)) {
		didAny = true;
	}
	if (chkDef && RunDefaultsScan (dialogId, defCheckedCount, defTotalIssues)) {
		didAny = true;
	}
	if (chkFav && RunFavoritesScan (dialogId, favCheckedCount, favTotalIssues)) {
		didAny = true;
	}
	if (chkView && RunViewsScan (dialogId, viewCheckedCount, viewTotalIssues)) {
		didAny = true;
	}
	if (chkDoc && RunDocsScan (dialogId, docCheckedCount, docTotalIssues)) {
		didAny = true;
	}

	UpdateTreeNodesDisplay (dialogId);
	RefreshResultList (dialogId);
	ApplyCurrentVisualHighlights ();

	if (!didAny) {
		SetStatusText (dialogId, GS::UniString (u"Zaškrtněte ve stromu vlevo alespoň jednu kontrolu."));
		return;
	}

	GS::UniString st (u"Kontrola dokončena:");
	bool hasPart = false;
	auto appendPart = [&] (std::u16string_view label, int count) {
		if (hasPart) {
			st += GS::UniString (" |");
		}
		st += GS::UniString (" ") + GS::UniString (label) + GS::UniString::Printf (" (%d)", count);
		hasPart = true;
	};

	if (chkAcc) {
		appendPart (u"Přesnost", static_cast<int> (gPaletteState.results.GetSize ()));
	}
	if (chkAttr) {
		appendPart (u"Atributy", attrTotalIssues);
	}
	if (chkProp) {
		appendPart (u"Vlastnosti", propTotalIssues);
	}
	if (chkElem) {
		appendPart (u"Prvky", elemTotalIssues);
	}
	if (chkDef) {
		appendPart (u"Výchozí", defTotalIssues);
	}
	if (chkFav) {
		appendPart (u"Oblíbené", favTotalIssues);
	}
	if (chkView) {
		appendPart (u"Zobrazení", viewTotalIssues);
	}
	if (chkDoc) {
		appendPart (u"Dokumentace", docTotalIssues);
	}
	st += GS::UniString (".");
	SetStatusText (dialogId, st);
}

static void RunSelectedScan (short dialogId)
{
	gSelectedCustomDimRowIndex = -1;
	RestoreAllDimensionHighlights ();
	(void) CegraVisual_Remove ();

	if (gPaletteState.selectedCategory == CategoryId::Accuracy) {
		if (RunAccuracyScan (dialogId, true)) {
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			ApplyCurrentVisualHighlights ();
			GS::UniString st = GS::UniString (u"Kontrola vybrané položky (Přesnost modelu): nalezeno ") +
				GS::ValueToUniString (static_cast<int> (gPaletteState.results.GetSize ())) +
				GS::UniString (u" odchylek.");
			SetStatusText (dialogId, st);
		}
		return;
	}

	const short subIdx = gPaletteState.selectedSubIdx;

	if (gPaletteState.selectedCategory == CategoryId::Attributes) {
		if (subIdx >= 0 && static_cast<UIndex> (subIdx) < gPaletteState.attrSubChecks.GetSize ()) {
			SubCheckState& sub = gPaletteState.attrSubChecks[subIdx];
			RunAttributeSubCheck (static_cast<AttributeSubCheckId> (subIdx), gPaletteState.disabledClassSystems, sub);
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané položky (") + sub.baseName +
				GS::UniString (u"): nalezeno ") + GS::ValueToUniString (static_cast<int> (sub.rows.GetSize ())) +
				GS::UniString (u" problémů."));
		} else {
			int totalIssues = 0;
			for (UIndex i = 0; i < gPaletteState.attrSubChecks.GetSize (); ++i) {
				SubCheckState& sub = gPaletteState.attrSubChecks[i];
				RunAttributeSubCheck (static_cast<AttributeSubCheckId> (i), gPaletteState.disabledClassSystems, sub);
				totalIssues += static_cast<int> (sub.rows.GetSize ());
			}
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané kategorie (Atributy): nalezeno celkem ") +
				GS::ValueToUniString (totalIssues) + GS::UniString (u" problémů."));
		}
		return;
	}

	if (gPaletteState.selectedCategory == CategoryId::Properties) {
		if (subIdx >= 0 && static_cast<UIndex> (subIdx) < gPaletteState.propSubChecks.GetSize ()) {
			SubCheckState& sub = gPaletteState.propSubChecks[subIdx];
			RunPropertySubCheck (static_cast<PropertySubCheckId> (subIdx), sub);
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané položky (") + sub.baseName +
				GS::UniString (u"): nalezeno ") + GS::ValueToUniString (static_cast<int> (sub.rows.GetSize ())) +
				GS::UniString (u" problémů."));
		} else {
			int totalIssues = 0;
			for (UIndex i = 0; i < gPaletteState.propSubChecks.GetSize (); ++i) {
				SubCheckState& sub = gPaletteState.propSubChecks[i];
				RunPropertySubCheck (static_cast<PropertySubCheckId> (i), sub);
				totalIssues += static_cast<int> (sub.rows.GetSize ());
			}
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané kategorie (Vlastnosti): nalezeno celkem ") +
				GS::ValueToUniString (totalIssues) + GS::UniString (u" problémů."));
		}
		return;
	}

	if (gPaletteState.selectedCategory == CategoryId::Elements) {
		if (subIdx >= 0 && static_cast<UIndex> (subIdx) < gPaletteState.elemSubChecks.GetSize ()) {
			SubCheckState& sub = gPaletteState.elemSubChecks[subIdx];
			RunElementSubCheck (static_cast<ElementSubCheckId> (subIdx), gPaletteState.disabledClassSystems, sub);
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			ApplyCurrentVisualHighlights ();
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané položky (") + sub.baseName +
				GS::UniString (u"): nalezeno ") + GS::ValueToUniString (static_cast<int> (sub.rows.GetSize ())) +
				GS::UniString (u" problémů."));
		} else {
			int totalIssues = 0;
			for (UIndex i = 0; i < gPaletteState.elemSubChecks.GetSize (); ++i) {
				SubCheckState& sub = gPaletteState.elemSubChecks[i];
				RunElementSubCheck (static_cast<ElementSubCheckId> (i), gPaletteState.disabledClassSystems, sub);
				totalIssues += static_cast<int> (sub.rows.GetSize ());
			}
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			ApplyCurrentVisualHighlights ();
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané kategorie (Prvky modelu): nalezeno celkem ") +
				GS::ValueToUniString (totalIssues) + GS::UniString (u" problémů."));
		}
		return;
	}

	if (gPaletteState.selectedCategory == CategoryId::Defaults) {
		if (subIdx >= 0 && static_cast<UIndex> (subIdx) < gPaletteState.defSubChecks.GetSize ()) {
			SubCheckState& sub = gPaletteState.defSubChecks[subIdx];
			RunDefaultSubCheck (static_cast<ElementSubCheckId> (subIdx), gPaletteState.disabledClassSystems, sub);
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané položky (") + sub.baseName +
				GS::UniString (u"): nalezeno ") + GS::ValueToUniString (static_cast<int> (sub.rows.GetSize ())) +
				GS::UniString (u" problémů."));
		} else {
			int totalIssues = 0;
			for (UIndex i = 0; i < gPaletteState.defSubChecks.GetSize (); ++i) {
				SubCheckState& sub = gPaletteState.defSubChecks[i];
				RunDefaultSubCheck (static_cast<ElementSubCheckId> (i), gPaletteState.disabledClassSystems, sub);
				totalIssues += static_cast<int> (sub.rows.GetSize ());
			}
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané kategorie (Výchozí nastavení nástrojů): nalezeno celkem ") +
				GS::ValueToUniString (totalIssues) + GS::UniString (u" problémů."));
		}
		return;
	}

	if (gPaletteState.selectedCategory == CategoryId::Favorites) {
		if (subIdx >= 0 && static_cast<UIndex> (subIdx) < gPaletteState.favSubChecks.GetSize ()) {
			SubCheckState& sub = gPaletteState.favSubChecks[subIdx];
			RunFavoriteSubCheck (static_cast<ElementSubCheckId> (subIdx), gPaletteState.disabledClassSystems, sub);
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané položky (") + sub.baseName +
				GS::UniString (u"): nalezeno ") + GS::ValueToUniString (static_cast<int> (sub.rows.GetSize ())) +
				GS::UniString (u" problémů."));
		} else {
			int totalIssues = 0;
			for (UIndex i = 0; i < gPaletteState.favSubChecks.GetSize (); ++i) {
				SubCheckState& sub = gPaletteState.favSubChecks[i];
				RunFavoriteSubCheck (static_cast<ElementSubCheckId> (i), gPaletteState.disabledClassSystems, sub);
				totalIssues += static_cast<int> (sub.rows.GetSize ());
			}
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané kategorie (Oblíbená nastavení): nalezeno celkem ") +
				GS::ValueToUniString (totalIssues) + GS::UniString (u" problémů."));
		}
		return;
	}

	if (gPaletteState.selectedCategory == CategoryId::Views) {
		if (subIdx >= 0 && static_cast<UIndex> (subIdx) < gPaletteState.viewSubChecks.GetSize ()) {
			SubCheckState& sub = gPaletteState.viewSubChecks[subIdx];
			RunViewSubCheck (static_cast<ViewSubCheckId> (subIdx), sub);
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané položky (") + sub.baseName +
				GS::UniString (u"): nalezeno ") + GS::ValueToUniString (static_cast<int> (sub.rows.GetSize ())) +
				GS::UniString (u" problémů."));
		} else {
			int totalIssues = 0;
			for (UIndex i = 0; i < gPaletteState.viewSubChecks.GetSize (); ++i) {
				SubCheckState& sub = gPaletteState.viewSubChecks[i];
				RunViewSubCheck (static_cast<ViewSubCheckId> (i), sub);
				totalIssues += static_cast<int> (sub.rows.GetSize ());
			}
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané kategorie (Zobrazení): nalezeno celkem ") +
				GS::ValueToUniString (totalIssues) + GS::UniString (u" problémů."));
		}
		return;
	}

	if (gPaletteState.selectedCategory == CategoryId::Docs) {
		if (subIdx >= 0 && static_cast<UIndex> (subIdx) < gPaletteState.docSubChecks.GetSize ()) {
			SubCheckState& sub = gPaletteState.docSubChecks[subIdx];
			RunDocSubCheck (static_cast<DocSubCheckId> (subIdx), sub);
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			ApplyCurrentVisualHighlights ();
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané položky (") + sub.baseName +
				GS::UniString (u"): nalezeno ") + GS::ValueToUniString (static_cast<int> (sub.rows.GetSize ())) +
				GS::UniString (u" problémů."));
		} else {
			int totalIssues = 0;
			for (UIndex i = 0; i < gPaletteState.docSubChecks.GetSize (); ++i) {
				SubCheckState& sub = gPaletteState.docSubChecks[i];
				RunDocSubCheck (static_cast<DocSubCheckId> (i), sub);
				totalIssues += static_cast<int> (sub.rows.GetSize ());
			}
			UpdateTreeNodesDisplay (dialogId);
			RefreshResultList (dialogId);
			ApplyCurrentVisualHighlights ();
			SetStatusText (dialogId,
				GS::UniString (u"Kontrola vybrané kategorie (Dokumentace): nalezeno celkem ") +
				GS::ValueToUniString (totalIssues) + GS::UniString (u" problémů."));
		}
		return;
	}
}

static API_ElemType GetParentToolboxElemType (const API_ElemType& rawType, bool& outIsSubElement)
{
	outIsSubElement = false;
	switch (rawType.typeID) {
		case API_ColumnSegmentID:
			outIsSubElement = true;
			return API_ElemType (API_ColumnID, rawType.variationID);
		case API_BeamSegmentID:
			outIsSubElement = true;
			return API_ElemType (API_BeamID, rawType.variationID);
		case API_CurtainWallSegmentID:
		case API_CurtainWallFrameID:
		case API_CurtainWallPanelID:
		case API_CurtainWallJunctionID:
		case API_CurtainWallAccessoryID:
			outIsSubElement = true;
			return API_ElemType (API_CurtainWallID, rawType.variationID);
		case API_RiserID:
		case API_TreadID:
		case API_StairStructureID:
			outIsSubElement = true;
			return API_ElemType (API_StairID, rawType.variationID);
		case API_RailingSegmentID:
		case API_RailingToprailID:
		case API_RailingHandrailID:
		case API_RailingRailID:
		case API_RailingPostID:
		case API_RailingInnerPostID:
		case API_RailingBalusterID:
		case API_RailingPanelID:
		case API_RailingNodeID:
		case API_RailingBalusterSetID:
		case API_RailingPatternID:
		case API_RailingToprailEndID:
		case API_RailingHandrailEndID:
		case API_RailingRailEndID:
		case API_RailingToprailConnectionID:
		case API_RailingHandrailConnectionID:
		case API_RailingRailConnectionID:
		case API_RailingEndFinishID:
			outIsSubElement = true;
			return API_ElemType (API_RailingID, rawType.variationID);
		default:
			return rawType;
	}
}

static void OpenDefaultToolSettingsForListRow (short dialogId, UIndex listIndex)
{
	const short subIdx = gPaletteState.selectedSubIdx;
	if (gPaletteState.selectedCategory != CategoryId::Defaults ||
		subIdx < 0 ||
		static_cast<UIndex> (subIdx) >= gPaletteState.defSubChecks.GetSize ()) {
		return;
	}

	const SubCheckState& sub = gPaletteState.defSubChecks[subIdx];
	if (listIndex == 0 || listIndex > sub.rows.GetSize ()) {
		return;
	}

	const AuditRow& row          = sub.rows[listIndex - 1];
	GS::UniString toolName;
	if (!row.cells.IsEmpty ()) {
		toolName = row.cells[0];
	} else {
		(void) ACAPI_Element_GetElemTypeName (row.toolType, toolName);
	}
	const API_ElemType rawType   = row.toolType;
	if (rawType == API_ZombieElemID) {
		ACAPI_WriteReport ("Kontrola modelu (Výchozí nastavení): řádek %u ('%s') nemá platný toolType.",
			false, static_cast<unsigned> (listIndex), toolName.ToCStr (0, MaxUSize, CC_UTF8).Get ());
		return;
	}

	bool isSubElem = false;
	API_ToolBoxItem tbi = {};
	tbi.type = GetParentToolboxElemType (rawType, isSubElem);

	(void) ACAPI_Toolbox_SetToolBoxMode (&tbi);
	const GSErrCode dlgErr = ACAPI_Dialog_SettingsDialog (&tbi);

	ACAPI_WriteReport ("Kontrola modelu (Výchozí nastavení): otevřen dialog nástroje '%s' (typeID=%d, varID=%u) -> dlgErr=%d",
		false,
		toolName.ToCStr (0, MaxUSize, CC_UTF8).Get (),
		static_cast<int> (tbi.type.typeID),
		static_cast<unsigned> (tbi.type.variationID),
		static_cast<int> (dlgErr));

	if (dlgErr == NoError) {
		for (UIndex i = 0; i < gPaletteState.defSubChecks.GetSize (); ++i) {
			SubCheckState& s = gPaletteState.defSubChecks[i];
			if (static_cast<short> (i) == subIdx || s.hasRun) {
				RunDefaultSubCheck (static_cast<ElementSubCheckId> (i), gPaletteState.disabledClassSystems, s);
			}
		}
		UpdateTreeNodesDisplay (dialogId);
		RefreshResultList (dialogId);
		SetStatusText (dialogId,
			GS::UniString (u"Výchozí nastavení nástroje '") + toolName +
			GS::UniString (u"' bylo upraveno (zbývá ") +
			GS::ValueToUniString (static_cast<int> (gPaletteState.defSubChecks[subIdx].rows.GetSize ())) +
			GS::UniString (u" problémů v této kontrole)."));
	}
}

static void OpenFavoriteSettingsForListRow (short dialogId, UIndex listIndex)
{
	const short subIdx = gPaletteState.selectedSubIdx;
	if (gPaletteState.selectedCategory != CategoryId::Favorites ||
		subIdx < 0 ||
		static_cast<UIndex> (subIdx) >= gPaletteState.favSubChecks.GetSize ()) {
		return;
	}

	const SubCheckState& sub = gPaletteState.favSubChecks[subIdx];
	if (listIndex == 0 || listIndex > sub.rows.GetSize ()) {
		return;
	}

	const AuditRow& row         = sub.rows[listIndex - 1];
	const GS::UniString favName = !row.favoriteName.IsEmpty () ? row.favoriteName : (!row.cells.IsEmpty () ? row.cells[0] : GS::UniString ());
	API_ElemType rawType        = row.toolType;
	if (favName.IsEmpty ()) {
		return;
	}

	API_Favorite favData (favName);
	BNZeroMemory (&favData.element, sizeof (API_Element));
	favData.memo.New ();
	BNZeroMemory (&favData.memo.Get (), sizeof (API_ElementMemo));
	favData.classifications.New ();
	favData.elemCategoryValues.New ();
	favData.properties.New ();
	favData.subElements.New ();
	favData.elementMarker.New ();
	BNZeroMemory (&favData.elementMarker.Get (), sizeof (API_Element));
	favData.memoMarker.New ();
	BNZeroMemory (&favData.memoMarker.Get (), sizeof (API_ElementMemo));

	const GSErrCode getFavErr = ACAPI_Favorite_Get (&favData);
	if (getFavErr == NoError && rawType == API_ZombieElemID) {
		rawType = favData.element.header.type;
	}

	auto disposeFavDataMemos = [&] () {
		if (favData.memo.HasValue ()) {
			ACAPI_DisposeElemMemoHdls (&favData.memo.Get ());
		}
		if (favData.memoMarker.HasValue ()) {
			ACAPI_DisposeElemMemoHdls (&favData.memoMarker.Get ());
		}
		if (favData.subElements.HasValue ()) {
			for (API_SubElement& se : favData.subElements.Get ()) {
				ACAPI_DisposeElemMemoHdls (&se.memo);
			}
		}
	};

	if (rawType == API_ZombieElemID) {
		ACAPI_WriteReport ("Kontrola modelu (Oblíbená nastavení): nepodařilo se zjistit typ prvku pro oblíbené '%s' (err=%d).",
			false, favName.ToCStr (0, MaxUSize, CC_UTF8).Get (), static_cast<int> (getFavErr));
		disposeFavDataMemos ();
		return;
	}

	/* 1. Aplikujeme Oblíbené nastavení do výchozího nastavení daného nástroje v Archicadu */
	bool appliedToDefault = false;
	auto favManager = ACAPI::Favorite::GetFavoriteManager ();
	auto favObjRes  = favManager.GetFavoriteByName (favName);
	if (favObjRes.IsOk ()) {
		auto elemDefRes = favObjRes->GetElementDefault ();
		if (elemDefRes.IsOk ()) {
			auto setDefRes = elemDefRes->SetAsArchicadDefault ();
			if (setDefRes.IsOk ()) {
				appliedToDefault = true;
			}
		}
	}

	if (!appliedToDefault && getFavErr == NoError) {
		API_Element fullMask;
		ACAPI_ELEMENT_MASK_SETFULL (fullMask);
		if (ACAPI_Element_ChangeDefaults (&favData.element,
				favData.memo.HasValue () ? &favData.memo.Get () : nullptr,
				&fullMask) == NoError) {
			appliedToDefault = true;
		}
		if (favData.classifications.HasValue ()) {
			for (const GS::Pair<API_Guid, API_Guid>& cl : favData.classifications.Get ()) {
				if (cl.second != APINULLGuid) {
					(void) ACAPI_Element_AddClassificationItemDefault (rawType, cl.second);
				}
			}
		}
		if (favData.elemCategoryValues.HasValue ()) {
			for (const API_ElemCategoryValue& cv : favData.elemCategoryValues.Get ()) {
				(void) ACAPI_Category_SetCategoryValueDefault (rawType, cv.category, cv);
			}
		}
		if (favData.properties.HasValue () && !favData.properties->IsEmpty ()) {
			(void) ACAPI_Element_SetPropertiesOfDefaultElem (rawType, favData.properties.Get ());
		}
	}

	/* 2. Aktivujeme nástroj v Toolboxu a otevřeme nativní dialog nastavení */
	bool isSubElem = false;
	API_ToolBoxItem tbi = {};
	tbi.type = GetParentToolboxElemType (rawType, isSubElem);

	(void) ACAPI_Toolbox_SetToolBoxMode (&tbi);
	const GSErrCode dlgErr = ACAPI_Dialog_SettingsDialog (&tbi);

	ACAPI_WriteReport ("Kontrola modelu (Oblíbená nastavení): otevřen dialog pro oblíbené '%s' (rawType=%d, toolboxType=%d, appliedDef=%d) -> dlgErr=%d",
		false,
		favName.ToCStr (0, MaxUSize, CC_UTF8).Get (),
		static_cast<int> (rawType.typeID),
		static_cast<int> (tbi.type.typeID),
		appliedToDefault ? 1 : 0,
		static_cast<int> (dlgErr));

	/* 3. Pokud uživatel potvrdil dialog tlačítkem OK, uložíme upravené výchozí nastavení zpět do Oblíbeného */
	if (dlgErr == NoError && getFavErr == NoError) {
		if (favData.memo.HasValue ()) {
			ACAPI_DisposeElemMemoHdls (&favData.memo.Get ());
			BNZeroMemory (&favData.memo.Get (), sizeof (API_ElementMemo));
		} else {
			favData.memo.New ();
			BNZeroMemory (&favData.memo.Get (), sizeof (API_ElementMemo));
		}

		favData.element.header.type = rawType;
		const GSErrCode getDefErr = ACAPI_Element_GetDefaults (&favData.element, &favData.memo.Get ());
		if (getDefErr == NoError) {
			/* Načteme aktualizované klasifikace výchozího prvku */
			GS::Array<GS::Pair<API_Guid, API_Guid>> updatedClassifications;
			if (ACAPI_Element_GetClassificationItemsDefault (rawType, updatedClassifications) == NoError) {
				favData.classifications = updatedClassifications;
			}

			/* Načteme aktualizované kategorie (rekonstrukce, konstrukční funkce, pozice) */
			GS::Array<API_ElemCategory> categoryList;
			if (ACAPI_Category_GetElementCategories (&categoryList) == NoError) {
				GS::Array<API_ElemCategoryValue> updatedCategories;
				for (const API_ElemCategory& cat : categoryList) {
					API_ElemCategoryValue catVal;
					BNZeroMemory (&catVal, sizeof (API_ElemCategoryValue));
					if (ACAPI_Category_GetCategoryValueDefault (rawType, cat, &catVal) == NoError) {
						updatedCategories.Push (catVal);
					}
				}
				favData.elemCategoryValues = updatedCategories;
			}

			/* Načteme aktualizované uživatelské vlastnosti */
			GS::Array<API_PropertyDefinition> propDefs;
			if (ACAPI_Element_GetPropertyDefinitionsOfDefaultElem (rawType, API_PropertyDefinitionFilter_UserDefined, propDefs) == NoError) {
				GS::Array<API_Property> allProps;
				if (ACAPI_Element_GetPropertyValuesOfDefaultElem (rawType, propDefs, allProps) == NoError) {
					GS::Array<API_Property> customProps;
					for (const API_Property& p : allProps) {
						if (!p.isDefault && p.definition.canValueBeEditable && p.status == API_Property_HasValue) {
							customProps.Push (p);
						}
					}
					favData.properties = customProps;
				}
			}

			/* Pokud nemáme podprvky, nepředáváme prázdné pole subElements, abychom nepřepsali původní stav */
			if (favData.subElements.HasValue () && favData.subElements->IsEmpty ()) {
				favData.subElements.Clear ();
			}
			if (favData.elementMarker.HasValue () && favData.elementMarker->header.type == API_ZombieElemID) {
				favData.elementMarker.Clear ();
				if (favData.memoMarker.HasValue ()) {
					ACAPI_DisposeElemMemoHdls (&favData.memoMarker.Get ());
					favData.memoMarker.Clear ();
				}
			}

			API_Element fullMask;
			ACAPI_ELEMENT_MASK_SETFULL (fullMask);
			GSErrCode chgFavErr = ACAPI_Favorite_Change (favData, &fullMask, APIMemoMask_All);
			if (chgFavErr != NoError) {
				(void) ACAPI_CallCommand ("Kontrola modelu - Aktualizace oblibeneho nastaveni", [&] () -> GSErrCode {
					chgFavErr = ACAPI_Favorite_Change (favData, &fullMask, APIMemoMask_All);
					return NoError;
				});
			}

			ACAPI_WriteReport ("Kontrola modelu (Oblíbená nastavení): uložení změn do oblíbeného '%s' -> chgFavErr=%d",
				false, favName.ToCStr (0, MaxUSize, CC_UTF8).Get (), static_cast<int> (chgFavErr));
		} else {
			ACAPI_WriteReport ("Kontrola modelu (Oblíbená nastavení): ACAPI_Element_GetDefaults selhalo pro typ %d (err=%d).",
				false, static_cast<int> (rawType.typeID), static_cast<int> (getDefErr));
		}

		for (UIndex i = 0; i < gPaletteState.favSubChecks.GetSize (); ++i) {
			SubCheckState& s = gPaletteState.favSubChecks[i];
			if (static_cast<short> (i) == subIdx || s.hasRun) {
				RunFavoriteSubCheck (static_cast<ElementSubCheckId> (i), gPaletteState.disabledClassSystems, s);
			}
		}
		UpdateTreeNodesDisplay (dialogId);
		RefreshResultList (dialogId);
		SetStatusText (dialogId,
			GS::UniString (u"Oblíbené nastavení '") + favName +
			GS::UniString (u"' bylo upraveno (zbývá ") +
			GS::ValueToUniString (static_cast<int> (gPaletteState.favSubChecks[subIdx].rows.GetSize ())) +
			GS::UniString (u" problémů v této kontrole)."));
	}

	disposeFavDataMemos ();
}

static void ResetSearch (short dialogId)
{
	gSelectedCustomDimRowIndex = -1;
	RestoreAllDimensionHighlights ();
	(void) CegraVisual_Remove ();
	gPaletteState.results.Clear ();
	gPaletteState.accuracyHasRun = false;
	gChlupListHighlightGuid	= APINULLGuid;
	auto clearSubChecks = [] (GS::Array<SubCheckState>& list) {
		for (SubCheckState& sub : list) {
			sub.rows.Clear ();
			sub.hasRun = false;
		}
	};
	clearSubChecks (gPaletteState.attrSubChecks);
	clearSubChecks (gPaletteState.propSubChecks);
	clearSubChecks (gPaletteState.elemSubChecks);
	clearSubChecks (gPaletteState.defSubChecks);
	clearSubChecks (gPaletteState.favSubChecks);
	clearSubChecks (gPaletteState.viewSubChecks);
	clearSubChecks (gPaletteState.docSubChecks);
	for (TreeCategoryNode& cat : gPaletteState.treeCategories) {
		for (TreeChildNode& ch : cat.children) {
			ch.hasRun     = false;
			ch.issueCount = 0;
		}
	}
	ACAPI_Selection_DeselectAll ();
	UpdateTreeNodesDisplay (dialogId);
	RefreshResultList (dialogId);
	SetStatusText (dialogId, GS::UniString (u"Výsledky a barevné zvýraznění byly vymazány."));
}

static UShort DoubleToUShortColor (double comp)
{
	const long v = std::lround (std::clamp (comp, 0.0, 1.0) * 65535.0);
	return static_cast<UShort> (std::clamp<long> (v, 0, 65535));
}

static void UpdateSettingsColorSwatches (short dialogId)
{
	DGUserSetBackgroundColor (dialogId, SettingsUsrHitColor,
		DoubleToUShortColor (sTempHitRgba.f_red),
		DoubleToUShortColor (sTempHitRgba.f_green),
		DoubleToUShortColor (sTempHitRgba.f_blue));
	DGUserRedrawItem (dialogId, SettingsUsrHitColor, true, true);

	DGUserSetBackgroundColor (dialogId, SettingsUsrRestColor,
		DoubleToUShortColor (sTempRestRgba.f_red),
		DoubleToUShortColor (sTempRestRgba.f_green),
		DoubleToUShortColor (sTempRestRgba.f_blue));
	DGUserRedrawItem (dialogId, SettingsUsrRestColor, true, true);
}

static void PopulateSettingsClassificationList (short dialogId)
{
	DGListSetTabFieldCount (dialogId, SettingsClassList, 2);
	DGListSetTabFieldData (dialogId, SettingsClassList, 1, 0, 28, DG_IS_CENTER, DG_IS_NOTRUNC, false, true);
	DGListSetTabFieldData (dialogId, SettingsClassList, 2, 28, 428, DG_IS_LEFT, DG_IS_TRUNCEND, false, true);
	DGListDeleteItem (dialogId, SettingsClassList, DG_ALL_ITEMS);

	sTempProjectClassSystems.Clear ();
	if (ACAPI_Classification_GetClassificationSystems (sTempProjectClassSystems) != NoError || sTempProjectClassSystems.IsEmpty ()) {
		(void) DGListInsertItem (dialogId, SettingsClassList, DG_LIST_BOTTOM);
		const short row = DGListGetItemCount (dialogId, SettingsClassList);
		if (row > 0) {
			DGListSetTabItemText (dialogId, SettingsClassList, row, 2,
				GS::UniString (u"(V projektu nejsou definovány žádné klasifikační systémy)"));
		}
		return;
	}

	for (UIndex i = 0; i < sTempProjectClassSystems.GetSize (); ++i) {
		const API_ClassificationSystem& sys = sTempProjectClassSystems[i];
		const bool isChecked = !sTempDisabledClassSystems.Contains (sys.guid);
		(void) DGListInsertItem (dialogId, SettingsClassList, DG_LIST_BOTTOM);
		const short row = DGListGetItemCount (dialogId, SettingsClassList);
		if (row <= 0) {
			continue;
		}
		const DG::Icon chkIcon (SysResModule, isChecked ? DG_LIST_CHECKEDICON : DG_LIST_UNCHECKEDICON);
		DGListSetTabItemIcon (dialogId, SettingsClassList, row, 1, chkIcon);
		GS::UniString label = sys.name;
		if (!sys.editionVersion.IsEmpty ()) {
			label += GS::UniString (" ") + sys.editionVersion;
		}
		DGListSetTabItemText (dialogId, SettingsClassList, row, 2, label);
		DGListSetItemUserData (dialogId, SettingsClassList, row, static_cast<DGUserData> (i + 1));
	}
}

static void FillSettingsDialogControls (
	short					dialogId,
	const API_RGBAColor&	hitCol,
	const API_RGBAColor&	restCol,
	Int32					decimals,
	bool					enableHl,
	short					dimPen,
	double					dimCircleDiamMm)
{
	sTempHitRgba			= hitCol;
	sTempRestRgba			= restCol;
	sTempDimHighlightPen	= std::clamp<short> (dimPen, 1, 255);
	DGSetItemValLong (dialogId, SettingsChkEnable, enableHl ? 1 : 0);
	DGSetItemValDouble (dialogId, SettingsRealHitOp, hitCol.f_alpha * 100.0);
	DGSetItemValDouble (dialogId, SettingsRealRestOp, restCol.f_alpha * 100.0);
	DGSetItemValLong (dialogId, SettingsIntDecimals, std::clamp<Int32> (decimals, 0, 10));
	DGSetItemValLong (dialogId, SettingsIntDimPen, sTempDimHighlightPen);
	DGSetItemValLong (dialogId, SettingsUsrDimPen, sTempDimHighlightPen);
	DGSetItemValDouble (dialogId, SettingsRealDimCircleMm, std::clamp (dimCircleDiamMm, 1.0, 50000.0));
	UpdateSettingsColorSwatches (dialogId);
	PopulateSettingsClassificationList (dialogId);
}

static void PickColorForSettings (short dialogId, bool isHitColor)
{
	API_RGBAColor& target = isHitColor ? sTempHitRgba : sTempRestRgba;
	UShort r = DoubleToUShortColor (target.f_red);
	UShort g = DoubleToUShortColor (target.f_green);
	UShort b = DoubleToUShortColor (target.f_blue);

	const GS::UniString title = isHitColor
		? GS::UniString (u"Barva zvýraznění nalezených prvků")
		: GS::UniString (u"Barva ostatních prvků v pohledu");

	if (DGGetColor (title, &r, &g, &b) != 0) {
		target.f_red	= r / 65535.0;
		target.f_green	= g / 65535.0;
		target.f_blue	= b / 65535.0;
		UpdateSettingsColorSwatches (dialogId);
	}
}

static void ToggleSettingsClassRow (short dialogId, short clickedRow)
{
	if (clickedRow > 0 && static_cast<UIndex> (clickedRow - 1) < sTempProjectClassSystems.GetSize ()) {
		const API_Guid& guid = sTempProjectClassSystems[clickedRow - 1].guid;
		if (sTempDisabledClassSystems.Contains (guid)) {
			sTempDisabledClassSystems.Delete (guid);
		} else {
			sTempDisabledClassSystems.Add (guid);
		}
		const bool isChecked = !sTempDisabledClassSystems.Contains (guid);
		DGListSetTabItemIcon (dialogId, SettingsClassList, clickedRow, 1,
			DG::Icon (SysResModule, isChecked ? DG_LIST_CHECKEDICON : DG_LIST_UNCHECKEDICON));
		DGListDeselectItem (dialogId, SettingsClassList, clickedRow);
	}
}

static short DGCALLBACK SettingsDialogHandler (short message, short dialogId, short item, DGUserData, DGMessageData msgData)
{
	switch (message) {
		case DG_MSG_INIT: {
			API_UCCallbackType ucb = {};
			ucb.type     = APIUserControlType_Pen;
			ucb.dialogID = dialogId;
			ucb.itemID   = SettingsUsrDimPen;
			(void) ACAPI_Dialog_SetUserControlCallback (&ucb);

			sTempDisabledClassSystems = gPaletteState.disabledClassSystems;
			FillSettingsDialogControls (dialogId, gHitRgba, gRestRgba, gListDecimals, gEnableHighlight, gDimHighlightPen, gStaticDimCircleDiamMm);
		} return 0;

		case DG_MSG_CHANGE:
			if (item == SettingsClassList) {
				const short sel = DGListGetSelected (dialogId, SettingsClassList, 0);
				ToggleSettingsClassRow (dialogId, sel);
				return 0;
			}
			if (item == SettingsUsrDimPen) {
				const short pen = std::clamp<short> (static_cast<short> (DGGetItemValLong (dialogId, SettingsUsrDimPen)), 1, 255);
				sTempDimHighlightPen = pen;
				DGSetItemValLong (dialogId, SettingsIntDimPen, pen);
				return 0;
			}
			if (item == SettingsIntDimPen) {
				const short pen = std::clamp<short> (static_cast<short> (DGGetItemValLong (dialogId, SettingsIntDimPen)), 1, 255);
				sTempDimHighlightPen = pen;
				DGSetItemValLong (dialogId, SettingsUsrDimPen, pen);
				return 0;
			}
			return 0;

		case DG_MSG_TRACK:
			if (item == SettingsUsrDimPen) {
				const short pen = std::clamp<short> (static_cast<short> (DGGetItemValLong (dialogId, SettingsUsrDimPen)), 1, 255);
				sTempDimHighlightPen = pen;
				DGSetItemValLong (dialogId, SettingsIntDimPen, pen);
				return 0;
			}
			if (item == SettingsIntDimPen) {
				const short pen = std::clamp<short> (static_cast<short> (DGGetItemValLong (dialogId, SettingsIntDimPen)), 1, 255);
				sTempDimHighlightPen = pen;
				DGSetItemValLong (dialogId, SettingsUsrDimPen, pen);
				return 0;
			}
			return 0;

		case DG_MSG_CLICK:
			if (item == SettingsClassList) {
				const auto* ev = reinterpret_cast<const DG::ListBoxItemMouseEventArg*> (msgData);
				if (ev != nullptr && ev->pressedItem > 0) {
					ToggleSettingsClassRow (dialogId, ev->pressedItem);
				}
				return 0;
			}
			if (item == SettingsBtnOk) {
				const double oH   = DGGetItemValDouble (dialogId, SettingsRealHitOp);
				const double oR   = DGGetItemValDouble (dialogId, SettingsRealRestOp);
				const double dCir = DGGetItemValDouble (dialogId, SettingsRealDimCircleMm);
				gHitRgba				= sTempHitRgba;
				gHitRgba.f_alpha		= std::clamp (oH / 100.0, 0.0, 1.0);
				gRestRgba				= sTempRestRgba;
				gRestRgba.f_alpha		= std::clamp (oR / 100.0, 0.0, 1.0);
				gListDecimals			= std::clamp<Int32> (static_cast<Int32> (DGGetItemValLong (dialogId, SettingsIntDecimals)), 0, 10);
				gEnableHighlight		= (DGGetItemValLong (dialogId, SettingsChkEnable) != 0);
				gDimHighlightPen		= std::clamp<short> (static_cast<short> (DGGetItemValLong (dialogId, SettingsIntDimPen)), 1, 255);
				gStaticDimCircleDiamMm	= std::clamp (dCir, 1.0, 50000.0);
				gPaletteState.disabledClassSystems = sTempDisabledClassSystems;

				SaveChlupPreferences ();
				if (gPaletteState.dialogId != 0) {
					RefreshResultList (gPaletteState.dialogId);
					ApplyCurrentVisualHighlights ();
				}
				return item;
			}
			if (item == SettingsBtnCancel || item == DG_CLOSEBOX) {
				return item;
			}
			if (item == SettingsBtnDefault) {
				API_RGBAColor defHit = {}, defRest = {};
				defHit.f_red	= 0.95; defHit.f_green	= 0.42; defHit.f_blue	= 0.42; defHit.f_alpha	= 1.00;
				defRest.f_red	= 0.55; defRest.f_green	= 0.55; defRest.f_blue	= 0.55; defRest.f_alpha	= 0.45;
				sTempDisabledClassSystems.Clear ();
				FillSettingsDialogControls (dialogId, defHit, defRest, 4, true, 20, 200.0);
				return 0;
			}
			if (item == SettingsBtnHitColor || item == SettingsUsrHitColor) {
				PickColorForSettings (dialogId, true);
				return 0;
			}
			if (item == SettingsBtnRestColor || item == SettingsUsrRestColor) {
				PickColorForSettings (dialogId, false);
				return 0;
			}
			return 0;

		default:
			break;
	}
	return 0;
}

static GSErrCode PaletteControlCallBack (Int32 referenceID, API_PaletteMessageID messageID, GS::IntPtr)
{
	if (referenceID != gPaletteState.dialogId || gPaletteState.dialogId == 0) {
		return NoError;
	}
	switch (messageID) {
		case APIPalMsg_ClosePalette:
			RestoreAllDimensionHighlights ();
			(void) CegraVisual_Remove ();
			DGModelessClose (gPaletteState.dialogId);
			break;
		case APIPalMsg_HidePalette_Begin:
			RestoreAllDimensionHighlights ();
			(void) CegraVisual_Remove ();
			DGHideModelessDialog (gPaletteState.dialogId);
			break;
		case APIPalMsg_HidePalette_End:
			DGShowModelessDialog (gPaletteState.dialogId, DG_DF_FIRST);
			ApplyCurrentVisualHighlights ();
			break;
		case APIPalMsg_DisableItems_Begin:
			SetPaletteItemsEnabled (gPaletteState.dialogId, false);
			break;
		case APIPalMsg_DisableItems_End:
			SetPaletteItemsEnabled (gPaletteState.dialogId, true);
			break;
		default:
			break;
	}
	return NoError;
}

static void HandleTreeSelectionChanged (short dialogId)
{
	const Int32 selTv = DGTreeViewGetSelected (dialogId, TreeChecks, DG_TVI_ROOT);
	if (selTv == DG_TVI_NONE) {
		return;
	}
	short catIdx = -1;
	short subIdx = -1;
	if (!DecodeTreeUserData (DGTreeViewGetItemUserData (dialogId, TreeChecks, selTv), catIdx, subIdx)) {
		return;
	}
	if (catIdx < 0 || static_cast<UIndex> (catIdx) >= gPaletteState.treeCategories.GetSize ()) {
		return;
	}
	gPaletteState.selectedCategory = static_cast<CategoryId> (catIdx);
	gPaletteState.selectedSubIdx   = subIdx;
	gSelectedCustomDimRowIndex     = -1;

	UpdateRightPanelLayout (dialogId);

	const bool liveSync = (DGGetItemValLong (dialogId, ChkLiveSync) != 0);
	if (liveSync) {
		RecolorModelByCurrentTab (dialogId, false);
	} else {
		RestoreAllDimensionHighlights ();
	}
}

static void HandleLiveSyncChanged (short dialogId)
{
	UpdateLiveSyncControlsState (dialogId);
	const bool liveSync = (DGGetItemValLong (dialogId, ChkLiveSync) != 0);
	if (liveSync) {
		RecolorModelByCurrentTab (dialogId, false);
	}
}

static short DGCALLBACK PaletteHandler (short message, short dialogId, short item, DGUserData, DGMessageData msgData)
{
	switch (message) {
		case DG_MSG_INIT:
			gPaletteState.dialogId = dialogId;
			if (gPaletteState.attrSubChecks.IsEmpty ()) {
				InitAttributeSubChecks (gPaletteState.attrSubChecks);
			}
			if (gPaletteState.propSubChecks.IsEmpty ()) {
				InitPropertySubChecks (gPaletteState.propSubChecks);
			}
			if (gPaletteState.elemSubChecks.IsEmpty ()) {
				InitElementSubChecks (gPaletteState.elemSubChecks);
			}
			if (gPaletteState.defSubChecks.IsEmpty ()) {
				InitDefaultSubChecks (gPaletteState.defSubChecks);
			}
			if (gPaletteState.favSubChecks.IsEmpty ()) {
				InitFavoriteSubChecks (gPaletteState.favSubChecks);
			}
			if (gPaletteState.viewSubChecks.IsEmpty ()) {
				InitViewSubChecks (gPaletteState.viewSubChecks);
			}
			if (gPaletteState.docSubChecks.IsEmpty ()) {
				InitDocSubChecks (gPaletteState.docSubChecks);
			}
			DGSetItemValLong (dialogId, BtnWall, 1);
			DGSetItemValLong (dialogId, BtnBeam, 1);
			DGSetItemValLong (dialogId, BtnColumn, 1);
			DGSetItemValLong (dialogId, BtnLine, 1);
			DGSetItemValLong (dialogId, BtnPolyLine, 1);
			DGSetItemValLong (dialogId, RadActiveView, 0);
			DGSetItemValLong (dialogId, RadWholeModel, 1);
			DGSetItemValLong (dialogId, RadHairy, 1);
			DGSetItemValLong (dialogId, RadModular, 0);
			DGSetItemValLong (dialogId, ChkLiveSync, 1);
			UpdateLiveSyncControlsState (dialogId);
			DGSetItemText (dialogId, EditModuleMm, "125");
			DGSetItemText (dialogId, EditToleranceMm, GS::UniString (u"0,1"));
			DGSetItemText (dialogId, EditAngleTolerance, GS::UniString (u"0,01"));
			DGSetDialogClientMinSize (dialogId, MinPaletteWidth, MinPaletteHeight);
			if (ACAPI_RegisterModelessWindow (dialogId, PaletteControlCallBack,
					API_PalEnabled_FloorPlan + API_PalEnabled_Section + API_PalEnabled_Elevation +
					API_PalEnabled_InteriorElevation + API_PalEnabled_Detail +
					API_PalEnabled_Worksheet + API_PalEnabled_Layout + API_PalEnabled_3D) != NoError) {
				ACAPI_WriteReport ("Kontrola modelu: ACAPI_RegisterModelessWindow selhalo.", true);
			}
			PopulateTreeChecks (dialogId);
			UpdateRightPanelLayout (dialogId);
			SetStatusText (dialogId, GS::UniString (u"Připraveno."));
			return 0;

		case DG_MSG_TREEITEMCLICK:
			if (item == TreeChecks) {
				const auto* ev = reinterpret_cast<const DG::TreeViewItemClickEventArg*> (msgData);
				if (ev != nullptr && ev->partId == DG_TVPI_STATEICON && ev->treeItem != DG_TVI_NONE) {
					ToggleTreeItemCheckbox (dialogId, ev->treeItem);
					return 1;
				}
			}
			return 0;

		case DG_MSG_ITEMHELP: {
			auto* helpEv = reinterpret_cast<DG::HelpEventArg*> (msgData);
			if (helpEv != nullptr && helpEv->subMessage == DG_HSM_TOOLTIP) {
				if (item == BtnCheck) {
					helpEv->toolTipText = GS::UniString (u"Zkontrolovat");
					return 1;
				}
				if (item == BtnCheckSelected) {
					helpEv->toolTipText = GS::UniString (u"Zkontrolovat pouze označené");
					return 1;
				}
				if (item == BtnReset) {
					helpEv->toolTipText = GS::UniString (u"Vymazat");
					return 1;
				}
				if (item == BtnRecolorByTab) {
					helpEv->toolTipText = GS::UniString (u"Přebarvit prvky modelu dle zvolené záložky");
					return 1;
				}
			}
		} return 0;

		case DG_MSG_CLICK: {
			if (item == DG_CLOSEBOX) {
				return item;
			}
			switch (item) {
				case BtnCheck:
					RunScan (dialogId);
					break;
				case BtnCheckSelected:
					RunSelectedScan (dialogId);
					break;
				case BtnReset:
					ResetSearch (dialogId);
					break;
				case BtnRecolorByTab:
					RecolorModelByCurrentTab (dialogId, true);
					break;
				case ChkLiveSync:
					HandleLiveSyncChanged (dialogId);
					break;
				case BtnSettings:
					(void) DGModalDialog (ACAPI_GetOwnResModule (), CHLUPATE_SETTINGS_ID, ACAPI_GetOwnResModule (), SettingsDialogHandler, 0);
					break;
				case BtnTreeCheckAll:
					SetAllTreeChecks (dialogId, true);
					break;
				case BtnTreeUncheckAll:
					SetAllTreeChecks (dialogId, false);
					break;
				case BtnInfoTolLength:
				case BtnInfoTolAngle:
					ShowToleranceHelpDialog ();
					break;
				case RadHairy:
				case RadModular:
					UpdateModeControls (dialogId);
					break;
				default:
					break;
			}
		} return 0;

		case DG_MSG_CHANGE:
			if (item == TreeChecks) {
				const auto* ev = reinterpret_cast<const DG::TreeViewChangeEventArg*> (msgData);
				if (ev == nullptr || ev->actionFlag == DG_TVCF_SELECTION) {
					HandleTreeSelectionChanged (dialogId);
				}
			} else if (item == ResultList) {
				if (gPaletteState.selectedCategory == CategoryId::Docs &&
					(gPaletteState.selectedSubIdx == static_cast<short> (DocSubCheckId::CustomTextDimensions) ||
					 gPaletteState.selectedSubIdx == static_cast<short> (DocSubCheckId::StaticDimensions))) {
					const short sel = DGListGetSelected (dialogId, ResultList, 0);
					Int32 newRowIdx = -1;
					if (sel > 0) {
						const UIndex idx = static_cast<UIndex> (DGListGetItemUserData (dialogId, ResultList, sel));
						if (idx > 0) {
							newRowIdx = static_cast<Int32> (idx - 1);
						}
					}
					if (newRowIdx != gSelectedCustomDimRowIndex) {
						gSelectedCustomDimRowIndex = newRowIdx;
						const bool liveSync = (DGGetItemValLong (dialogId, ChkLiveSync) != 0);
						if (liveSync || !gDimPenBackup.IsEmpty () || !gStaticDimCircles.IsEmpty ()) {
							RecolorModelByCurrentTab (dialogId, !liveSync);
						}
					}
				}
			} else if (item == ChkLiveSync) {
				HandleLiveSyncChanged (dialogId);
			} else if (item == RadHairy || item == RadModular) {
				UpdateModeControls (dialogId);
			}
			return 0;

		case DG_MSG_DOUBLECLICK:
			if (item == ResultList) {
				const short sel = DGListGetSelected (dialogId, ResultList, 0);
				if (sel > 0) {
					const UIndex idx = static_cast<UIndex> (DGListGetItemUserData (dialogId, ResultList, sel));
					if (gPaletteState.selectedCategory == CategoryId::Accuracy) {
						SelectResultAndZoom (idx, false);
					} else if (gPaletteState.selectedSubIdx < 0 && idx > 0) {
						/* Poklikání v přehledu nadřazené kategorie přepne na danou dílčí kontrolu ve stromu */
						const short catIdx = static_cast<short> (gPaletteState.selectedCategory);
						if (catIdx >= 0 && static_cast<UIndex> (catIdx) < gPaletteState.treeCategories.GetSize ()) {
							const TreeCategoryNode& cat = gPaletteState.treeCategories[catIdx];
							if (idx - 1 < cat.children.GetSize ()) {
								const Int32 childTv = cat.children[idx - 1].tvItem;
								if (childTv != DG_TVI_NONE) {
									DGTreeViewSelectItem (dialogId, TreeChecks, childTv);
									HandleTreeSelectionChanged (dialogId);
								}
							}
						}
					} else if (gPaletteState.selectedCategory == CategoryId::Defaults &&
							   gPaletteState.selectedSubIdx >= 0) {
						OpenDefaultToolSettingsForListRow (dialogId, idx);
					} else if (gPaletteState.selectedCategory == CategoryId::Favorites &&
							   gPaletteState.selectedSubIdx >= 0) {
						OpenFavoriteSettingsForListRow (dialogId, idx);
					} else if ((gPaletteState.selectedCategory == CategoryId::Elements ||
								gPaletteState.selectedCategory == CategoryId::Docs) &&
							   gPaletteState.selectedSubIdx >= 0) {
						if (gPaletteState.selectedCategory == CategoryId::Docs &&
							(gPaletteState.selectedSubIdx == static_cast<short> (DocSubCheckId::CustomTextDimensions) ||
							 gPaletteState.selectedSubIdx == static_cast<short> (DocSubCheckId::StaticDimensions)) &&
							idx > 0) {
							gSelectedCustomDimRowIndex = static_cast<Int32> (idx - 1);
							const bool liveSync = (DGGetItemValLong (dialogId, ChkLiveSync) != 0);
							if (liveSync || !gDimPenBackup.IsEmpty () || !gStaticDimCircles.IsEmpty ()) {
								RecolorModelByCurrentTab (dialogId, !liveSync);
							}
						}
						SelectResultAndZoom (idx, false);
					}
				}
			}
			return 0;

		case DG_MSG_CONTEXTMENU:
			if (item == ResultList &&
				(gPaletteState.selectedCategory == CategoryId::Accuracy ||
				 ((gPaletteState.selectedCategory == CategoryId::Elements ||
				   gPaletteState.selectedCategory == CategoryId::Docs) &&
				  gPaletteState.selectedSubIdx >= 0))) {
				const auto* ev = reinterpret_cast<const DG::ContextMenuEventArg*> (msgData);
				if (ev != nullptr && ev->listItem > 0) {
					const UIndex listIndex = static_cast<UIndex> (DGListGetItemUserData (dialogId, ResultList, ev->listItem));
					ShowListContextMenu (dialogId, listIndex, ev);
				}
			}
			return 0;

		case DG_MSG_GROW: {
			const short hGrow = DGGetHGrow (msgData);
			const short vGrow = DGGetVGrow (msgData);
			DGBeginMoveGrowItems (dialogId);
			DGGrowItem (dialogId, TreeChecks, 0, vGrow);
			DGMoveItem (dialogId, BtnCheck, 0, vGrow);
			DGMoveItem (dialogId, BtnCheckSelected, 0, vGrow);
			DGMoveItem (dialogId, BtnReset, 0, vGrow);
			DGMoveItem (dialogId, BtnRecolorByTab, 0, vGrow);
			DGMoveItem (dialogId, ChkLiveSync, 0, vGrow);
			DGGrowItem (dialogId, SepVertical, 0, vGrow);
			DGGrowItem (dialogId, TxtRightHeader, hGrow, 0);
			DGGrowItem (dialogId, TxtSubChecksHdr, hGrow, 0);
			DGGrowItem (dialogId, ResultList, hGrow, vGrow);
			DGMoveGrowItem (dialogId, TxtStatus, 0, vGrow, hGrow, 0);
			DGEndMoveGrowItems (dialogId);
		} return 0;

		case DG_MSG_CLOSE:
			(void) ACAPI_Notification_CatchSelectionChange (nullptr);
			ACAPI_UnregisterModelessWindow (dialogId);
			ACAPI_Selection_DeselectAll ();
			gSelectedCustomDimRowIndex = -1;
			RestoreAllDimensionHighlights ();
			(void) CegraVisual_Remove ();
			gChlupListHighlightGuid	= APINULLGuid;
			gPaletteState.dialogId	= 0;
			gPaletteState.results.Clear ();
			return 0;
	}
	return 0;
}

static GSErrCode ChlupProjectEventHandler (API_NotifyEventID notifID, Int32 /*param*/)
{
	switch (notifID) {
		case APINotify_PreSave:
		case APINotify_Close:
		case APINotify_Quit:
			RestoreAllDimensionHighlights ();
			break;
		case APINotify_Save:
			if (gPaletteState.dialogId != 0) {
				ApplyCurrentVisualHighlights ();
			}
			break;
		default:
			break;
	}
	return NoError;
}

static void OpenPalette (void)
{
	if (gPaletteState.dialogId != 0) {
		DGShowModelessDialog (gPaletteState.dialogId, DG_DF_FIRST);
		(void) ACAPI_Notification_CatchSelectionChange (ChlupOnSelectionChange);
		return;
	}
	gPaletteState.dialogId = DGModelessInit (ACAPI_GetOwnResModule (), CHLUPATE_PALETTE_ID, ACAPI_GetOwnResModule (), PaletteHandler, 0, 1);
	if (gPaletteState.dialogId != 0) {
		(void) ACAPI_Notification_CatchSelectionChange (ChlupOnSelectionChange);
	}
}

} // namespace

static GSErrCode MenuCommandHandler (const API_MenuParams* menuParams)
{
	if (menuParams == nullptr) {
		return APIERR_BADPARS;
	}
	if (menuParams->menuItemRef.menuResID == CHLUPATE_MENU_STR_ID && menuParams->menuItemRef.itemIndex == ChlupateOpenPalette) {
		OpenPalette ();
	}
	return NoError;
}

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, CHLUPATE_ADDON_INFO_STR_ID, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, CHLUPATE_ADDON_INFO_STR_ID, 2, ACAPI_GetOwnResModule ());
	return APIAddon_Normal;
}

GSErrCode RegisterInterface (void)
{
	return ACAPI_MenuItem_RegisterMenu (CHLUPATE_MENU_STR_ID, CHLUPATE_MENU_PROMPT_STR_ID, MenuCode_UserDef, MenuFlag_Default);
}

GSErrCode Initialize (void)
{
	LoadChlupPreferences ();
	(void) ACAPI_ProjectOperation_CatchProjectEvent (
		APINotify_PreSave | APINotify_Save | APINotify_Close | APINotify_Quit,
		ChlupProjectEventHandler);
	return ACAPI_MenuItem_InstallMenuHandler (CHLUPATE_MENU_STR_ID, MenuCommandHandler);
}

GSErrCode FreeData (void)
{
	RestoreAllDimensionHighlights ();
	(void) ACAPI_ProjectOperation_CatchProjectEvent (0, nullptr);
	(void) ACAPI_Notification_CatchSelectionChange (nullptr);
	SaveChlupPreferences ();
	return NoError;
}
