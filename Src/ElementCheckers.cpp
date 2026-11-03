#include "ElementCheckers.hpp"
#include "ACAPI_Database.h"
#include "ACAPI_Environment.h"
#include "ACAPI_Goodies.h"
#include "ACAPI_Navigator.h"
#include "API_AttributeIndexUtils.hpp"
#include "APIdefs_Attributes.h"
#include "APIdefs_Elements.h"
#include "APIdefs_Environment.h"
#include "APIdefs_Favorite.h"
#include "APIdefs_LibraryParts.h"
#include "APIdefs_Navigator.h"
#include "APIdefs_Properties.h"
#include "BM.hpp"
#include "HashSet.hpp"
#include "HashTable.hpp"

#include <cmath>
#include <cstring>
#include <functional>

namespace {

enum ElemCapFlags : UInt32 {
	kElemCap_None               = 0x0000,
	kElemCap_Layer              = 0x0004,
	kElemCap_StructuralFunction = 0x0008,
	kElemCap_Position           = 0x0010,
	kElemCap_Classification     = 0x0100
};

static bool ExtractMainGuidUpper (const char* unIdStr, char outMainGuid[37])
{
	if (unIdStr == nullptr) {
		return false;
	}
	const char* p = unIdStr;
	while (*p == ' ' || *p == '\t') {
		++p;
	}
	if (*p == '{') {
		++p;
	}
	for (int i = 0; i < 36; ++i) {
		char c = p[i];
		if (c == '\0' || c == '}') {
			return false;
		}
		if (c >= 'a' && c <= 'z') {
			c = static_cast<char> (c - 'a' + 'A');
		}
		outMainGuid[i] = c;
	}
	outMainGuid[36] = '\0';
	return true;
}

static bool IsStandaloneModelObject (const API_ObjectType& obj, GS::HashTable<Int32, bool>& libPartCache)
{
	/* Vyloučení značek řezů, pohledů, interiérových pohledů, detailů, pracovních listů, změn a oken/dveří */
	if (obj.ownerType != API_ZombieElemID || obj.owner != APINULLGuid) {
		return false;
	}
	if (obj.libInd <= 0) {
		return false;
	}
	if (libPartCache.ContainsKey (obj.libInd)) {
		return libPartCache[obj.libInd];
	}

	API_LibPart libPart;
	BNZeroMemory (&libPart, sizeof (API_LibPart));
	libPart.index = obj.libInd;
	if (ACAPI_LibraryPart_Get (&libPart) != NoError) {
		if (libPart.location != nullptr) {
			delete libPart.location;
		}
		libPartCache.Put (obj.libInd, false);
		return false;
	}
	if (libPart.location != nullptr) {
		delete libPart.location;
		libPart.location = nullptr;
	}
	if (libPart.missingDef || libPart.isTemplate || !libPart.isPlaceable) {
		libPartCache.Put (obj.libInd, false);
		return false;
	}

	/* Kontrola, zda podtyp knihovního prvku vychází z "Model Element" (103E8D2C-8230-42E1-9597-46F84CCE28C0) */
	static const char* kModelElementMainGuid = "103E8D2C-8230-42E1-9597-46F84CCE28C0";
	char mainGuid[37] = {};
	API_LibPart curPart = libPart;

	bool isModelElem = false;
	for (int depth = 0; depth < 32; ++depth) {
		if (ExtractMainGuidUpper (curPart.ownUnID, mainGuid)) {
			if (std::strcmp (mainGuid, kModelElementMainGuid) == 0) {
				isModelElem = true;
				break;
			}
		}
		if (!ExtractMainGuidUpper (curPart.parentUnID, mainGuid) ||
			std::strcmp (mainGuid, "00000000-0000-0000-0000-000000000000") == 0) {
			break;
		}
		API_LibPart parentPart;
		BNZeroMemory (&parentPart, sizeof (API_LibPart));
		CHCopyC (curPart.parentUnID, parentPart.ownUnID);
		if (ACAPI_LibraryPart_Search (&parentPart, false, false) != NoError) {
			if (parentPart.location != nullptr) {
				delete parentPart.location;
			}
			break;
		}
		if (parentPart.location != nullptr) {
			delete parentPart.location;
			parentPart.location = nullptr;
		}
		curPart = parentPart;
	}

	libPartCache.Put (obj.libInd, isModelElem);
	return isModelElem;
}

static UInt32 GetElementCapabilities (
	const API_Element&			elem,
	GS::HashTable<Int32, bool>&	libPartCache,
	bool&						outIs3DCapable,
	bool						isPlacedElement = true)
{
	outIs3DCapable = false;
	switch (elem.header.type.typeID) {
		/* Hlavní konstrukční a modelové prvky: Vrstva + Nosnost + Umístění + Klasifikace */
		case API_WallID:
		case API_ColumnID:
		case API_BeamID:
		case API_LampID:
		case API_SlabID:
		case API_RoofID:
		case API_MeshID:
		case API_CurtainWallID:
		case API_ShellID:
		case API_MorphID:
		case API_StairID:
		case API_RailingID:
		case API_ExternalElemID:
			outIs3DCapable = true;
			return kElemCap_Layer | kElemCap_StructuralFunction | kElemCap_Position | kElemCap_Classification;

		/* Objekt: u vložených prvků pouze samostatné modelové objekty (nikoliv značky řezů/pohledů/detailů),
		 * u výchozího nastavení nástroje Objekt a Oblíbených vždy modelový prvek. */
		case API_ObjectID:
			if (!isPlacedElement || IsStandaloneModelObject (elem.object, libPartCache)) {
				outIs3DCapable = true;
				return kElemCap_Layer | kElemCap_StructuralFunction | kElemCap_Position | kElemCap_Classification;
			}
			return kElemCap_None;

		/* Výplně otvorů (Okno, Dveře, Střešní okno): Nosnost + Umístění + Klasifikace (vrstva je zděděna ze stěny/střechy) */
		case API_WindowID:
		case API_DoorID:
		case API_SkylightID:
			outIs3DCapable = true;
			return kElemCap_StructuralFunction | kElemCap_Position | kElemCap_Classification;

		/* Zóna: Vrstva + Klasifikace (nemá Nosnost konstrukce ani Umístění) */
		case API_ZoneID:
			outIs3DCapable = true;
			return kElemCap_Layer | kElemCap_Classification;

		/* Průraz (Opening) a podprvky lehkého obvodového pláště, schodiště, zábradlí, sloupu a trámu: Klasifikace */
		case API_OpeningID:
		case API_CurtainWallSegmentID:
		case API_CurtainWallFrameID:
		case API_CurtainWallPanelID:
		case API_CurtainWallJunctionID:
		case API_CurtainWallAccessoryID:
		case API_RiserID:
		case API_TreadID:
		case API_StairStructureID:
		case API_RailingPostID:
		case API_RailingToprailID:
		case API_RailingHandrailID:
		case API_RailingRailID:
		case API_RailingInnerPostID:
		case API_RailingBalusterID:
		case API_RailingPanelID:
		case API_RailingSegmentID:
		case API_BeamSegmentID:
		case API_ColumnSegmentID:
			outIs3DCapable = true;
			return kElemCap_Classification;

		/* 2D dokumentační prvky a značky s vlastní vrstvou (nemají Klasifikaci, Nosnost ani Umístění) */
		case API_DimensionID:
		case API_RadialDimensionID:
		case API_LevelDimensionID:
		case API_AngleDimensionID:
		case API_TextID:
		case API_LabelID:
		case API_HatchID:
		case API_LineID:
		case API_PolyLineID:
		case API_ArcID:
		case API_CircleID:
		case API_SplineID:
		case API_HotspotID:
		case API_CutPlaneID:
		case API_PictureID:
		case API_DrawingID:
		case API_DetailID:
		case API_ElevationID:
		case API_InteriorElevationID:
		case API_WorksheetID:
		case API_ChangeMarkerID:
			outIs3DCapable = false;
			return kElemCap_Layer;

		default:
			return kElemCap_None;
	}
}

static GS::UniString GetElemTypeName (const API_ElemType& elemType)
{
	GS::UniString name;
	if (ACAPI_Element_GetElemTypeName (elemType, name) == NoError && !name.IsEmpty ()) {
		return name;
	}
	switch (elemType.typeID) {
		case API_WallID:					return GS::UniString (u"Zeď");
		case API_ColumnID:					return GS::UniString (u"Sloup");
		case API_BeamID:					return GS::UniString (u"Trám");
		case API_WindowID:					return GS::UniString (u"Okno");
		case API_DoorID:					return GS::UniString (u"Dveře");
		case API_ObjectID:					return GS::UniString (u"Objekt");
		case API_LampID:					return GS::UniString (u"Lampa");
		case API_SlabID:					return GS::UniString (u"Deska");
		case API_RoofID:					return GS::UniString (u"Střecha");
		case API_MeshID:					return GS::UniString (u"Síť");
		case API_ZoneID:					return GS::UniString (u"Zóna");
		case API_CurtainWallID:				return GS::UniString (u"Lehký obvodový plášť");
		case API_ShellID:					return GS::UniString (u"Skořepina");
		case API_SkylightID:				return GS::UniString (u"Střešní okno");
		case API_MorphID:					return GS::UniString (u"Morf");
		case API_StairID:					return GS::UniString (u"Schodiště");
		case API_RailingID:					return GS::UniString (u"Zábradlí");
		case API_OpeningID:					return GS::UniString (u"Průraz");
		case API_BeamSegmentID:				return GS::UniString (u"Segment trámu");
		case API_ColumnSegmentID:			return GS::UniString (u"Segment sloupu");
		case API_CurtainWallSegmentID:		return GS::UniString (u"Segment LOP");
		case API_CurtainWallFrameID:		return GS::UniString (u"Rám LOP");
		case API_CurtainWallPanelID:		return GS::UniString (u"Panel LOP");
		case API_CurtainWallJunctionID:		return GS::UniString (u"Spoj LOP");
		case API_CurtainWallAccessoryID:	return GS::UniString (u"Doplněk LOP");
		case API_RiserID:					return GS::UniString (u"Podstupnice");
		case API_TreadID:					return GS::UniString (u"Stupeň schodiště");
		case API_StairStructureID:			return GS::UniString (u"Konstrukce schodiště");
		case API_RailingPostID:				return GS::UniString (u"Sloupek zábradlí");
		case API_RailingToprailID:			return GS::UniString (u"Horní madlo");
		case API_RailingHandrailID:			return GS::UniString (u"Boční madlo");
		case API_RailingRailID:				return GS::UniString (u"Vodorovná tyč zábradlí");
		case API_RailingInnerPostID:		return GS::UniString (u"Vnitřní sloupek zábradlí");
		case API_RailingBalusterID:			return GS::UniString (u"Svislá tyč zábradlí");
		case API_RailingPanelID:			return GS::UniString (u"Panel zábradlí");
		case API_RailingSegmentID:			return GS::UniString (u"Segment zábradlí");
		case API_HatchID:					return GS::UniString (u"Výplň");
		case API_LineID:					return GS::UniString (u"Čára");
		case API_PolyLineID:				return GS::UniString (u"Lomená čára");
		case API_ArcID:						return GS::UniString (u"Oblouk");
		case API_CircleID:					return GS::UniString (u"Kružnice");
		case API_SplineID:					return GS::UniString (u"Křivka");
		case API_HotspotID:					return GS::UniString (u"Aktivní bod");
		case API_TextID:					return GS::UniString (u"Text");
		case API_LabelID:					return GS::UniString (u"Popiska");
		case API_DimensionID:				return GS::UniString (u"Kóta");
		case API_RadialDimensionID:			return GS::UniString (u"Radiální kóta");
		case API_LevelDimensionID:			return GS::UniString (u"Výšková kóta");
		case API_AngleDimensionID:			return GS::UniString (u"Úhlová kóta");
		case API_CutPlaneID:				return GS::UniString (u"Řez");
		case API_ElevationID:				return GS::UniString (u"Pohled");
		case API_InteriorElevationID:		return GS::UniString (u"Interiérový pohled");
		case API_DetailID:					return GS::UniString (u"Detail");
		case API_WorksheetID:				return GS::UniString (u"Pracovní list");
		case API_PictureID:					return GS::UniString (u"Obrázek");
		case API_DrawingID:					return GS::UniString (u"Kresba");
		case API_ChangeMarkerID:			return GS::UniString (u"Značka změny");
		default:							return GS::UniString (u"Prvek");
	}
}

static GS::UniString GetAttrTypeNameCZ (API_AttrTypeID typeID)
{
	switch (typeID) {
		case API_LayerID:				return GS::UniString (u"Vrstva");
		case API_LinetypeID:			return GS::UniString (u"Typ čáry");
		case API_FilltypeID:			return GS::UniString (u"Typ výplně");
		case API_CompWallID:			return GS::UniString (u"Sendvičová konstrukce");
		case API_MaterialID:			return GS::UniString (u"Povrchový materiál");
		case API_ZoneCatID:				return GS::UniString (u"Kategorie zóny");
		case API_ProfileID:				return GS::UniString (u"Složitý profil");
		case API_BuildingMaterialID:	return GS::UniString (u"Stavební materiál");
		default:						return GS::UniString (u"Atribut");
	}
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

static GS::UniString LookupStoryName (short floorInd, const GS::HashTable<short, GS::UniString>& storyMap)
{
	if (storyMap.ContainsKey (floorInd)) {
		return storyMap[floorInd];
	}
	return GS::ValueToUniString (static_cast<int> (floorInd)) + GS::UniString (u". podlaží");
}

static GS::UniString LookupLayerName (
	const API_Element&					elem,
	UInt32								caps,
	GS::HashTable<Int32, GS::UniString>& layerCache)
{
	if ((caps & kElemCap_Layer) == 0) {
		return GS::UniString (u"— (dle nadřazeného prvku)");
	}
	const API_AttributeIndex layerInd = elem.header.layer;
	const Int32 idxVal = layerInd.ToInt32_Deprecated ();
	if (layerCache.ContainsKey (idxVal)) {
		return layerCache[idxVal];
	}
	API_Attribute attr;
	BNZeroMemory (&attr, sizeof (API_Attribute));
	attr.header.typeID = API_LayerID;
	attr.header.index  = layerInd;
	GS::UniString name;
	if (ACAPI_Attribute_Get (&attr) == NoError) {
		if (attr.header.uniStringNamePtr != nullptr) {
			name = *attr.header.uniStringNamePtr;
		} else {
			name = GS::UniString (attr.header.name, CC_Default);
		}
	} else {
		name = GS::UniString (u"CHYBÍ!");
	}
	layerCache.Put (idxVal, name);
	return name;
}

static bool DoesAttributeExistCached (
	API_AttrTypeID					typeID,
	API_AttributeIndex				index,
	GS::HashTable<UInt64, bool>&	existsCache)
{
	const Int32 idxVal = index.ToInt32_Deprecated ();
	/* U povrchového materiálu znamená index 0 platný stav "Dle stavebního materiálu / kategorie" */
	if (typeID == API_MaterialID && idxVal == 0) {
		return true;
	}
	if (idxVal <= 0) {
		return false;
	}
	const UInt64 key = (static_cast<UInt64> (typeID) << 32) | static_cast<UInt32> (idxVal);
	if (existsCache.ContainsKey (key)) {
		return existsCache[key];
	}
	API_Attribute attr;
	BNZeroMemory (&attr, sizeof (API_Attribute));
	attr.header.typeID = typeID;
	attr.header.index  = index;
	const bool exists = (ACAPI_Attribute_Get (&attr) == NoError);
	if (exists && typeID == API_MaterialID && attr.material.texture.fileLoc != nullptr) {
		delete attr.material.texture.fileLoc;
		attr.material.texture.fileLoc = nullptr;
	}
	existsCache.Put (key, exists);
	return exists;
}

static GS::UniString GetElementIdStr (const API_Guid& guid)
{
	GS::UniString infoStr;
	if (ACAPI_Element_GetElementInfoString (&guid, &infoStr) == NoError && !infoStr.IsEmpty ()) {
		return infoStr;
	}
	return GS::UniString (u"—");
}

using MissingAttrRecordFn = std::function<void (API_AttrTypeID, API_AttributeIndex, const GS::UniString&)>;

static void CheckSingleAttrRef (
	API_AttrTypeID					refAttrType,
	API_AttributeIndex				refAttrIndex,
	std::u16string_view				usageRole,
	GS::HashTable<UInt64, bool>&	existsCache,
	const MissingAttrRecordFn&		recordFn)
{
	if (!DoesAttributeExistCached (refAttrType, refAttrIndex, existsCache)) {
		recordFn (refAttrType, refAttrIndex, GS::UniString (usageRole));
	}
}

static void CheckOverriddenMaterialRef (
	const API_OverriddenAttribute&	ovrMat,
	std::u16string_view				usageRole,
	GS::HashTable<UInt64, bool>&	existsCache,
	const MissingAttrRecordFn&		recordFn)
{
	if (ovrMat.hasValue) {
		CheckSingleAttrRef (API_MaterialID, ovrMat.value, usageRole, existsCache, recordFn);
	}
}

static void CheckShellBaseAttrRefs (
	const API_ShellBaseType&		sb,
	GS::HashTable<UInt64, bool>&	existsCache,
	const MissingAttrRecordFn&		recordFn)
{
	CheckSingleAttrRef (API_LinetypeID, sb.ltypeInd, u"Obrysová čára", existsCache, recordFn);
	CheckSingleAttrRef (API_LinetypeID, sb.sectContLtype, u"Řezová obrysová čára", existsCache, recordFn);
	CheckSingleAttrRef (API_LinetypeID, sb.aboveViewLineType, u"Čára nad rovinou řezu", existsCache, recordFn);

	if (sb.modelElemStructureType == API_BasicStructure) {
		CheckSingleAttrRef (API_BuildingMaterialID, sb.buildingMaterial, u"Stavební materiál", existsCache, recordFn);
	} else if (sb.modelElemStructureType == API_CompositeStructure) {
		CheckSingleAttrRef (API_CompWallID, sb.composite, u"Sendvičová konstrukce", existsCache, recordFn);
	}
	if (sb.useFloorFill && !sb.use3DHatching) {
		CheckSingleAttrRef (API_FilltypeID, sb.floorFillInd, u"Povrchová výplň", existsCache, recordFn);
	}
	CheckOverriddenMaterialRef (sb.topMat, u"Horní povrch", existsCache, recordFn);
	CheckOverriddenMaterialRef (sb.sidMat, u"Boční povrch", existsCache, recordFn);
	CheckOverriddenMaterialRef (sb.botMat, u"Spodní povrch", existsCache, recordFn);
}

static void CheckBeamSegmentAttrRefs (
	const API_BeamSegmentType&		seg,
	GS::HashTable<UInt64, bool>&	existsCache,
	const MissingAttrRecordFn&		recordFn)
{
	if (seg.assemblySegmentData.modelElemStructureType == API_BasicStructure) {
		CheckSingleAttrRef (API_BuildingMaterialID, seg.assemblySegmentData.buildingMaterial, u"Stavební materiál segmentu", existsCache, recordFn);
	} else if (seg.assemblySegmentData.modelElemStructureType == API_ProfileStructure) {
		CheckSingleAttrRef (API_ProfileID, seg.assemblySegmentData.profileAttr, u"Složitý profil segmentu", existsCache, recordFn);
	}
	CheckOverriddenMaterialRef (seg.leftMaterial, u"Levý povrch segmentu", existsCache, recordFn);
	CheckOverriddenMaterialRef (seg.topMaterial, u"Horní povrch segmentu", existsCache, recordFn);
	CheckOverriddenMaterialRef (seg.rightMaterial, u"Pravý povrch segmentu", existsCache, recordFn);
	CheckOverriddenMaterialRef (seg.bottomMaterial, u"Spodní povrch segmentu", existsCache, recordFn);
	CheckOverriddenMaterialRef (seg.endsMaterial, u"Čelní povrch segmentu", existsCache, recordFn);
	CheckOverriddenMaterialRef (seg.extrusionMaterial, u"Povrch vysunutí segmentu", existsCache, recordFn);
}

static void CheckColumnSegmentAttrRefs (
	const API_ColumnSegmentType&	seg,
	GS::HashTable<UInt64, bool>&	existsCache,
	const MissingAttrRecordFn&		recordFn)
{
	if (seg.assemblySegmentData.modelElemStructureType == API_BasicStructure) {
		CheckSingleAttrRef (API_BuildingMaterialID, seg.assemblySegmentData.buildingMaterial, u"Stavební materiál segmentu", existsCache, recordFn);
	} else if (seg.assemblySegmentData.modelElemStructureType == API_ProfileStructure) {
		CheckSingleAttrRef (API_ProfileID, seg.assemblySegmentData.profileAttr, u"Složitý profil segmentu", existsCache, recordFn);
	}
	if (seg.venThick > 1e-6) {
		CheckSingleAttrRef (API_BuildingMaterialID, seg.venBuildingMaterial, u"Stavební materiál obkladu", existsCache, recordFn);
	}
	CheckOverriddenMaterialRef (seg.extrusionSurfaceMaterial, u"Boční povrch segmentu", existsCache, recordFn);
	CheckOverriddenMaterialRef (seg.endsMaterial, u"Čelní povrch segmentu", existsCache, recordFn);
}

static void CheckGdlParamsMissingAttrs (
	API_AddParType**				params,
	GS::HashTable<UInt64, bool>&	existsCache,
	const MissingAttrRecordFn&		recordFn)
{
	if (params == nullptr) {
		return;
	}
	const GSSize nBytes = BMGetHandleSize (reinterpret_cast<GSHandle> (params));
	if (nBytes <= 0) {
		return;
	}
	const Int32 nParams = static_cast<Int32> (nBytes / sizeof (API_AddParType));
	for (Int32 i = 0; i < nParams; ++i) {
		const API_AddParType& par = (*params)[i];
		if (par.typeMod != API_ParSimple) {
			continue;
		}
		if ((par.flags & (API_ParFlg_Hidden | API_ParFlg_SHidden | API_ParFlg_Disabled)) != 0) {
			continue;
		}
		API_AttrTypeID attrType = API_ZombieAttrID;
		switch (par.typeID) {
			case APIParT_LineTyp:			attrType = API_LinetypeID;			break;
			case APIParT_Mater:				attrType = API_MaterialID;			break;
			case APIParT_FillPat:			attrType = API_FilltypeID;			break;
			case APIParT_BuildingMaterial:	attrType = API_BuildingMaterialID;	break;
			case APIParT_Profile:			attrType = API_ProfileID;			break;
			default:						continue;
		}
		const Int32 idxVal = static_cast<Int32> (std::lround (par.value.real));
		if (idxVal <= 0) {
			/* V GDL skriptech hodnota <= 0 u atributových parametrů znamená "Vypnuto / Dle nastavení" */
			continue;
		}
		const API_AttributeIndex attrIdx = ACAPI_CreateAttributeIndex (idxVal);
		if (!DoesAttributeExistCached (attrType, attrIdx, existsCache)) {
			GS::UniString paramLabel (u"GDL: ");
			if (GS::ucslen (par.uDescname) > 0) {
				paramLabel += GS::UniString (par.uDescname);
			} else {
				paramLabel += GS::UniString (par.name, CC_Default);
			}
			recordFn (attrType, attrIdx, paramLabel);
		}
	}
}

static void InspectElementMissingAttributes (
	const API_Element&				elem,
	const API_ElementMemo*			preloadedMemo,
	UInt32							caps,
	GS::HashTable<UInt64, bool>&	existsCache,
	const MissingAttrRecordFn&		recordFn)
{
	/* Vrstva v hlavičce prvku (pouze u prvků s vlastní vrstvou) */
	if ((caps & kElemCap_Layer) != 0) {
		CheckSingleAttrRef (API_LayerID, elem.header.layer, u"Vrstva", existsCache, recordFn);
	}

	switch (elem.header.type.typeID) {
		case API_WallID: {
			CheckSingleAttrRef (API_LinetypeID, elem.wall.contLtype, u"Obrysová čára", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.wall.belowViewLineType, u"Čára pod rovinou řezu", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.wall.aboveViewLineType, u"Čára nad rovinou řezu", existsCache, recordFn);

			if (elem.wall.modelElemStructureType == API_BasicStructure) {
				CheckSingleAttrRef (API_BuildingMaterialID, elem.wall.buildingMaterial, u"Stavební materiál", existsCache, recordFn);
			} else if (elem.wall.modelElemStructureType == API_CompositeStructure) {
				CheckSingleAttrRef (API_CompWallID, elem.wall.composite, u"Sendvičová konstrukce", existsCache, recordFn);
			} else if (elem.wall.modelElemStructureType == API_ProfileStructure) {
				CheckSingleAttrRef (API_ProfileID, elem.wall.profileAttr, u"Složitý profil", existsCache, recordFn);
			}
			CheckOverriddenMaterialRef (elem.wall.refMat, u"Povrch u referenční čáry", existsCache, recordFn);
			CheckOverriddenMaterialRef (elem.wall.oppMat, u"Protilehlý povrch", existsCache, recordFn);
			CheckOverriddenMaterialRef (elem.wall.sidMat, u"Boční povrch", existsCache, recordFn);
			break;
		}
		case API_SlabID: {
			CheckSingleAttrRef (API_LinetypeID, elem.slab.ltypeInd, u"Obrysová čára", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.slab.sectContLtype, u"Řezová obrysová čára", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.slab.hiddenContourLineType, u"Skrytá obrysová čára", existsCache, recordFn);

			if (elem.slab.modelElemStructureType == API_BasicStructure) {
				CheckSingleAttrRef (API_BuildingMaterialID, elem.slab.buildingMaterial, u"Stavební materiál", existsCache, recordFn);
			} else if (elem.slab.modelElemStructureType == API_CompositeStructure) {
				CheckSingleAttrRef (API_CompWallID, elem.slab.composite, u"Sendvičová konstrukce", existsCache, recordFn);
			}
			if (elem.slab.useFloorFill && !elem.slab.use3DHatching) {
				CheckSingleAttrRef (API_FilltypeID, elem.slab.floorFillInd, u"Povrchová výplň", existsCache, recordFn);
			}
			CheckOverriddenMaterialRef (elem.slab.topMat, u"Horní povrch", existsCache, recordFn);
			CheckOverriddenMaterialRef (elem.slab.sideMat, u"Boční povrch", existsCache, recordFn);
			CheckOverriddenMaterialRef (elem.slab.botMat, u"Spodní povrch", existsCache, recordFn);
			break;
		}
		case API_RoofID:
			CheckShellBaseAttrRefs (elem.roof.shellBase, existsCache, recordFn);
			break;
		case API_ShellID:
			CheckShellBaseAttrRefs (elem.shell.shellBase, existsCache, recordFn);
			break;
		case API_BeamID: {
			CheckSingleAttrRef (API_LinetypeID, elem.beam.aboveViewLineType, u"Čára nad rovinou řezu", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.beam.refLtype, u"Referenční čára", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.beam.cutContourLineType, u"Řezová čára", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.beam.hiddenLineType, u"Skrytá čára", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.beam.belowViewLineType, u"Obrysová čára", existsCache, recordFn);
			if (elem.beam.useCoverFill && !elem.beam.useCoverFillFromSurface) {
				CheckSingleAttrRef (API_FilltypeID, elem.beam.coverFillType, u"Povrchová výplň", existsCache, recordFn);
			}
			if (preloadedMemo != nullptr) {
				if (preloadedMemo->beamSegments != nullptr) {
					const UInt32 nSegs = static_cast<UInt32> (BMGetPtrSize (reinterpret_cast<GSPtr> (preloadedMemo->beamSegments)) / sizeof (API_BeamSegmentType));
					const UInt32 count = (elem.beam.nSegments > 0 && elem.beam.nSegments <= nSegs) ? elem.beam.nSegments : nSegs;
					for (UInt32 s = 0; s < count; ++s) {
						CheckBeamSegmentAttrRefs (preloadedMemo->beamSegments[s], existsCache, recordFn);
					}
				}
			} else if (elem.header.hasMemo && elem.beam.nSegments > 0 && elem.header.guid != APINULLGuid) {
				API_ElementMemo memo = {};
				if (ACAPI_Element_GetMemo (elem.header.guid, &memo, APIMemoMask_BeamSegment) == NoError && memo.beamSegments != nullptr) {
					for (UInt32 s = 0; s < elem.beam.nSegments; ++s) {
						CheckBeamSegmentAttrRefs (memo.beamSegments[s], existsCache, recordFn);
					}
				}
				ACAPI_DisposeElemMemoHdls (&memo);
			}
			break;
		}
		case API_BeamSegmentID:
			CheckBeamSegmentAttrRefs (elem.beamSegment, existsCache, recordFn);
			break;
		case API_ColumnID: {
			CheckSingleAttrRef (API_LinetypeID, elem.column.belowViewLineType, u"Obrysová čára", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.column.contLtype, u"Řezová čára", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.column.venLineType, u"Čára obkladu", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.column.hiddenLineType, u"Skrytá čára", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.column.aboveViewLineType, u"Čára nad rovinou řezu", existsCache, recordFn);
			if (elem.column.useCoverFill && !elem.column.useCoverFillFromSurface) {
				CheckSingleAttrRef (API_FilltypeID, elem.column.coverFillType, u"Povrchová výplň", existsCache, recordFn);
			}
			if (preloadedMemo != nullptr) {
				if (preloadedMemo->columnSegments != nullptr) {
					const UInt32 nSegs = static_cast<UInt32> (BMGetPtrSize (reinterpret_cast<GSPtr> (preloadedMemo->columnSegments)) / sizeof (API_ColumnSegmentType));
					const UInt32 count = (elem.column.nSegments > 0 && elem.column.nSegments <= nSegs) ? elem.column.nSegments : nSegs;
					for (UInt32 s = 0; s < count; ++s) {
						CheckColumnSegmentAttrRefs (preloadedMemo->columnSegments[s], existsCache, recordFn);
					}
				}
			} else if (elem.header.hasMemo && elem.column.nSegments > 0 && elem.header.guid != APINULLGuid) {
				API_ElementMemo memo = {};
				if (ACAPI_Element_GetMemo (elem.header.guid, &memo, APIMemoMask_ColumnSegment) == NoError && memo.columnSegments != nullptr) {
					for (UInt32 s = 0; s < elem.column.nSegments; ++s) {
						CheckColumnSegmentAttrRefs (memo.columnSegments[s], existsCache, recordFn);
					}
				}
				ACAPI_DisposeElemMemoHdls (&memo);
			}
			break;
		}
		case API_ColumnSegmentID:
			CheckColumnSegmentAttrRefs (elem.columnSegment, existsCache, recordFn);
			break;
		case API_MeshID: {
			CheckSingleAttrRef (API_LinetypeID, elem.mesh.ltypeInd, u"Obrysová čára", existsCache, recordFn);
			CheckSingleAttrRef (API_BuildingMaterialID, elem.mesh.buildingMaterial, u"Stavební materiál", existsCache, recordFn);
			if (elem.mesh.useFloorFill && !elem.mesh.use3DHatching) {
				CheckSingleAttrRef (API_FilltypeID, elem.mesh.floorFillInd, u"Povrchová výplň", existsCache, recordFn);
			}
			CheckOverriddenMaterialRef (elem.mesh.topMat, u"Horní povrch", existsCache, recordFn);
			CheckOverriddenMaterialRef (elem.mesh.sideMat, u"Boční povrch", existsCache, recordFn);
			CheckOverriddenMaterialRef (elem.mesh.botMat, u"Spodní povrch", existsCache, recordFn);
			break;
		}
		case API_MorphID: {
			CheckSingleAttrRef (API_LinetypeID, elem.morph.cutLineType, u"Řezová čára", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.morph.uncutLineType, u"Obrysová čára", existsCache, recordFn);
			CheckSingleAttrRef (API_LinetypeID, elem.morph.overheadLineType, u"Čára nad rovinou řezu", existsCache, recordFn);
			CheckSingleAttrRef (API_BuildingMaterialID, elem.morph.buildingMaterial, u"Stavební materiál", existsCache, recordFn);
			if (elem.morph.useCoverFillType && !elem.morph.use3DHatching) {
				CheckSingleAttrRef (API_FilltypeID, elem.morph.coverFillType, u"Povrchová výplň", existsCache, recordFn);
			}
			CheckOverriddenMaterialRef (elem.morph.material, u"Povrchový materiál", existsCache, recordFn);
			break;
		}
		case API_ZoneID: {
			CheckSingleAttrRef (API_ZoneCatID, elem.zone.catInd, u"Kategorie zóny", existsCache, recordFn);
			if (elem.zone.useContourLine) {
				CheckSingleAttrRef (API_LinetypeID, elem.zone.floorContLType, u"Obrysová čára", existsCache, recordFn);
			}
			if (elem.zone.useFloorFill && !elem.zone.use3DHatching) {
				CheckSingleAttrRef (API_FilltypeID, elem.zone.floorFillInd, u"Povrchová výplň", existsCache, recordFn);
			}
			CheckSingleAttrRef (API_MaterialID, elem.zone.material, u"Povrchový materiál", existsCache, recordFn);
			if (preloadedMemo != nullptr) {
				CheckGdlParamsMissingAttrs (preloadedMemo->params, existsCache, recordFn);
			} else if (elem.header.hasMemo && elem.header.guid != APINULLGuid) {
				API_ElementMemo memo = {};
				if (ACAPI_Element_GetMemo (elem.header.guid, &memo, APIMemoMask_AddPars) == NoError) {
					CheckGdlParamsMissingAttrs (memo.params, existsCache, recordFn);
				}
				ACAPI_DisposeElemMemoHdls (&memo);
			}
			break;
		}
		case API_ObjectID:
		case API_LampID: {
			if (!elem.object.useObjLtypes) {
				CheckSingleAttrRef (API_LinetypeID, elem.object.ltypeInd, u"Obrysová čára", existsCache, recordFn);
			}
			if (!elem.object.useObjMaterials) {
				CheckSingleAttrRef (API_MaterialID, elem.object.mat, u"Povrchový materiál", existsCache, recordFn);
			}
			if (!elem.object.useObjSectAttrs) {
				CheckSingleAttrRef (API_FilltypeID, elem.object.sectFill, u"Výplň", existsCache, recordFn);
			}
			if (preloadedMemo != nullptr) {
				CheckGdlParamsMissingAttrs (preloadedMemo->params, existsCache, recordFn);
			} else if (elem.header.hasMemo && elem.header.guid != APINULLGuid) {
				API_ElementMemo memo = {};
				if (ACAPI_Element_GetMemo (elem.header.guid, &memo, APIMemoMask_AddPars) == NoError) {
					CheckGdlParamsMissingAttrs (memo.params, existsCache, recordFn);
				}
				ACAPI_DisposeElemMemoHdls (&memo);
			}
			break;
		}
		case API_WindowID:
		case API_DoorID: {
			const API_OpeningBaseType& ob = elem.window.openingBase;
			if (!ob.useObjLtypes) {
				CheckSingleAttrRef (API_LinetypeID, ob.ltypeInd, u"Obrysová čára", existsCache, recordFn);
				CheckSingleAttrRef (API_LinetypeID, ob.cutLineType, u"Řezová čára", existsCache, recordFn);
				CheckSingleAttrRef (API_LinetypeID, ob.aboveViewLineType, u"Čára nad rovinou řezu", existsCache, recordFn);
				CheckSingleAttrRef (API_LinetypeID, ob.belowViewLineType, u"Čára pod rovinou řezu", existsCache, recordFn);
			}
			if (!ob.useObjMaterials) {
				CheckSingleAttrRef (API_MaterialID, ob.mat, u"Povrchový materiál", existsCache, recordFn);
			}
			if (!ob.useObjSectAttrs) {
				CheckSingleAttrRef (API_FilltypeID, ob.sectFill, u"Výplň", existsCache, recordFn);
			}
			if (preloadedMemo != nullptr) {
				CheckGdlParamsMissingAttrs (preloadedMemo->params, existsCache, recordFn);
			} else if (elem.header.hasMemo && elem.header.guid != APINULLGuid) {
				API_ElementMemo memo = {};
				if (ACAPI_Element_GetMemo (elem.header.guid, &memo, APIMemoMask_AddPars) == NoError) {
					CheckGdlParamsMissingAttrs (memo.params, existsCache, recordFn);
				}
				ACAPI_DisposeElemMemoHdls (&memo);
			}
			break;
		}
		case API_SkylightID: {
			const API_OpeningBaseType& ob = elem.skylight.openingBase;
			if (!ob.useObjLtypes) {
				CheckSingleAttrRef (API_LinetypeID, ob.ltypeInd, u"Obrysová čára", existsCache, recordFn);
				CheckSingleAttrRef (API_LinetypeID, ob.cutLineType, u"Řezová čára", existsCache, recordFn);
				CheckSingleAttrRef (API_LinetypeID, ob.aboveViewLineType, u"Čára nad rovinou řezu", existsCache, recordFn);
				CheckSingleAttrRef (API_LinetypeID, ob.belowViewLineType, u"Čára pod rovinou řezu", existsCache, recordFn);
			}
			if (!ob.useObjMaterials) {
				CheckSingleAttrRef (API_MaterialID, ob.mat, u"Povrchový materiál", existsCache, recordFn);
			}
			if (!ob.useObjSectAttrs) {
				CheckSingleAttrRef (API_FilltypeID, ob.sectFill, u"Výplň", existsCache, recordFn);
			}
			if (preloadedMemo != nullptr) {
				CheckGdlParamsMissingAttrs (preloadedMemo->params, existsCache, recordFn);
			} else if (elem.header.hasMemo && elem.header.guid != APINULLGuid) {
				API_ElementMemo memo = {};
				if (ACAPI_Element_GetMemo (elem.header.guid, &memo, APIMemoMask_AddPars) == NoError) {
					CheckGdlParamsMissingAttrs (memo.params, existsCache, recordFn);
				}
				ACAPI_DisposeElemMemoHdls (&memo);
			}
			break;
		}
		case API_HatchID: {
			CheckSingleAttrRef (API_LinetypeID, elem.hatch.ltypeInd, u"Obrysová čára", existsCache, recordFn);
			if (elem.hatch.hatchType == API_FillHatch) {
				CheckSingleAttrRef (API_FilltypeID, elem.hatch.fillInd, u"Typ výplně", existsCache, recordFn);
			} else if (elem.hatch.hatchType == API_BuildingMaterialHatch) {
				CheckSingleAttrRef (API_BuildingMaterialID, elem.hatch.buildingMaterial, u"Stavební materiál", existsCache, recordFn);
			}
			break;
		}
		case API_LineID:
			CheckSingleAttrRef (API_LinetypeID, elem.line.ltypeInd, u"Typ čáry", existsCache, recordFn);
			break;
		case API_PolyLineID:
			CheckSingleAttrRef (API_LinetypeID, elem.polyLine.ltypeInd, u"Typ čáry", existsCache, recordFn);
			break;
		case API_ArcID:
			CheckSingleAttrRef (API_LinetypeID, elem.arc.ltypeInd, u"Typ čáry", existsCache, recordFn);
			break;
		case API_CircleID:
			CheckSingleAttrRef (API_LinetypeID, elem.circle.ltypeInd, u"Typ čáry", existsCache, recordFn);
			break;
		case API_SplineID:
			CheckSingleAttrRef (API_LinetypeID, elem.spline.ltypeInd, u"Typ čáry", existsCache, recordFn);
			break;
		default:
			break;
	}
}

static GS::UniString FormatMissingClassificationsList (
	const GS::Array<API_ClassificationSystem>&				activeSystems,
	const GS::Array<GS::Pair<API_Guid, API_Guid>>&			sysItemPairs)
{
	GS::UniString missingSystemsStr;
	for (const API_ClassificationSystem& sys : activeSystems) {
		bool hasClassInSys = false;
		for (const auto& pair : sysItemPairs) {
			if (pair.first == sys.guid && pair.second != APINULLGuid) {
				hasClassInSys = true;
				break;
			}
		}
		if (!hasClassInSys) {
			if (!missingSystemsStr.IsEmpty ()) {
				missingSystemsStr += GS::UniString (", ");
			}
			missingSystemsStr += sys.name;
			if (!sys.editionVersion.IsEmpty ()) {
				missingSystemsStr += GS::UniString (" ") + sys.editionVersion;
			}
		}
	}
	return missingSystemsStr;
}

/* ============================================================================
 * 3: PRVKY MODELU (Vložené prvky)
 * ============================================================================ */

static void CheckMissingAttributesInElements (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::HashTable<short, GS::UniString> storyMap;
	BuildStoryNameMap (storyMap);
	GS::HashTable<UInt64, bool> existsCache;
	GS::HashTable<Int32, bool>  libPartCache;

	GS::Array<API_Guid> allElems;
	if (ACAPI_Element_GetElemList (API_ZombieElemID, &allElems) != NoError) {
		return;
	}

	for (const API_Guid& guid : allElems) {
		API_Element elem;
		BNZeroMemory (&elem, sizeof (API_Element));
		elem.header.guid = guid;
		if (ACAPI_Element_Get (&elem) != NoError) {
			continue;
		}
		bool is3DCapable = false;
		const UInt32 caps = GetElementCapabilities (elem, libPartCache, is3DCapable, true);
		if (caps == kElemCap_None) {
			continue;
		}

		const GS::UniString elemTypeName = GetElemTypeName (elem.header.type);
		const GS::UniString elemIdStr    = GetElementIdStr (guid);
		const GS::UniString storyName    = LookupStoryName (elem.header.floorInd, storyMap);

		InspectElementMissingAttributes (elem, nullptr, caps, existsCache,
			[&] (API_AttrTypeID refAttrType, API_AttributeIndex refAttrIndex, const GS::UniString& usageRole) {
				AuditRow row;
				row.elemGuid    = guid;
				row.is3DCapable = is3DCapable;
				row.attrType    = refAttrType;
				row.attrIndex   = refAttrIndex;
				row.cells.Push (elemTypeName);
				row.cells.Push (elemIdStr);
				row.cells.Push (storyName);
				row.cells.Push (GetAttrTypeNameCZ (refAttrType));
				row.cells.Push (usageRole);
				inOutState.rows.Push (row);
			});
	}
}

static void CheckUnclassifiedElements (
	const GS::HashSet<API_Guid>&	disabledClassificationSystems,
	SubCheckState&					inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::Array<API_ClassificationSystem> allSystems;
	if (ACAPI_Classification_GetClassificationSystems (allSystems) != NoError || allSystems.IsEmpty ()) {
		return;
	}

	GS::Array<API_ClassificationSystem> activeSystems;
	for (const API_ClassificationSystem& sys : allSystems) {
		if (!disabledClassificationSystems.Contains (sys.guid)) {
			activeSystems.Push (sys);
		}
	}
	if (activeSystems.IsEmpty ()) {
		return;
	}

	GS::HashTable<short, GS::UniString> storyMap;
	BuildStoryNameMap (storyMap);
	GS::HashTable<Int32, GS::UniString> layerCache;
	GS::HashTable<Int32, bool>          libPartCache;

	GS::Array<API_Guid> allElems;
	if (ACAPI_Element_GetElemList (API_ZombieElemID, &allElems) != NoError) {
		return;
	}

	for (const API_Guid& guid : allElems) {
		API_Element elem;
		BNZeroMemory (&elem, sizeof (API_Element));
		elem.header.guid = guid;
		if (ACAPI_Element_Get (&elem) != NoError) {
			continue;
		}
		bool is3DCapable = false;
		const UInt32 caps = GetElementCapabilities (elem, libPartCache, is3DCapable, true);
		if ((caps & kElemCap_Classification) == 0) {
			continue;
		}

		GS::Array<GS::Pair<API_Guid, API_Guid>> sysItemPairs;
		if (ACAPI_Element_GetClassificationItems (guid, sysItemPairs) != NoError) {
			continue;
		}

		const GS::UniString missingSystemsStr = FormatMissingClassificationsList (activeSystems, sysItemPairs);
		if (!missingSystemsStr.IsEmpty ()) {
			AuditRow row;
			row.elemGuid    = guid;
			row.is3DCapable = is3DCapable;
			row.cells.Push (GetElemTypeName (elem.header.type));
			row.cells.Push (GetElementIdStr (guid));
			row.cells.Push (LookupStoryName (elem.header.floorInd, storyMap));
			row.cells.Push (LookupLayerName (elem, caps, layerCache));
			row.cells.Push (missingSystemsStr);
			inOutState.rows.Push (row);
		}
	}
}

static void CheckUndefinedCategoryValue (
	API_ElemCategoryID	categoryID,
	UInt32				requiredCapBit,
	const char*			undefGuidStr,
	SubCheckState&		inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	const API_Guid undefValueGuid = APIGuidFromString (undefGuidStr);

	API_ElemCategory elemCategory;
	BNZeroMemory (&elemCategory, sizeof (API_ElemCategory));
	elemCategory.categoryID = categoryID;

	GS::HashTable<short, GS::UniString> storyMap;
	BuildStoryNameMap (storyMap);
	GS::HashTable<Int32, GS::UniString> layerCache;
	GS::HashTable<Int32, bool>          libPartCache;

	GS::Array<API_Guid> allElems;
	if (ACAPI_Element_GetElemList (API_ZombieElemID, &allElems) != NoError) {
		return;
	}

	for (const API_Guid& guid : allElems) {
		API_Element elem;
		BNZeroMemory (&elem, sizeof (API_Element));
		elem.header.guid = guid;
		if (ACAPI_Element_Get (&elem) != NoError) {
			continue;
		}
		bool is3DCapable = false;
		const UInt32 caps = GetElementCapabilities (elem, libPartCache, is3DCapable, true);
		if ((caps & requiredCapBit) == 0) {
			continue;
		}

		API_ElemCategoryValue catVal;
		BNZeroMemory (&catVal, sizeof (API_ElemCategoryValue));
		if (ACAPI_Category_GetCategoryValue (guid, elemCategory, &catVal) != NoError) {
			continue;
		}

		if (catVal.guid == undefValueGuid || catVal.guid == APINULLGuid) {
			AuditRow row;
			row.elemGuid    = guid;
			row.is3DCapable = is3DCapable;
			row.cells.Push (GetElemTypeName (elem.header.type));
			row.cells.Push (GetElementIdStr (guid));
			row.cells.Push (LookupStoryName (elem.header.floorInd, storyMap));
			row.cells.Push (LookupLayerName (elem, caps, layerCache));
			inOutState.rows.Push (row);
		}
	}
}

static void CheckElementsOnArchicadLayer (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::HashTable<short, GS::UniString> storyMap;
	BuildStoryNameMap (storyMap);
	GS::HashTable<Int32, bool>          libPartCache;

	GS::Array<API_Guid> allElems;
	if (ACAPI_Element_GetElemList (API_ZombieElemID, &allElems) != NoError) {
		return;
	}

	for (const API_Guid& guid : allElems) {
		API_Element elem;
		BNZeroMemory (&elem, sizeof (API_Element));
		elem.header.guid = guid;
		if (ACAPI_Element_Get (&elem) != NoError) {
			continue;
		}
		bool is3DCapable = false;
		const UInt32 caps = GetElementCapabilities (elem, libPartCache, is3DCapable, true);
		if ((caps & kElemCap_Layer) == 0) {
			continue;
		}

		if (elem.header.layer == APIApplicationLayerAttributeIndex) {
			AuditRow row;
			row.elemGuid    = guid;
			row.is3DCapable = is3DCapable;
			row.cells.Push (GetElemTypeName (elem.header.type));
			row.cells.Push (GetElementIdStr (guid));
			row.cells.Push (LookupStoryName (elem.header.floorInd, storyMap));
			inOutState.rows.Push (row);
		}
	}
}

/* ============================================================================
 * 4: VÝCHOZÍ NASTAVENÍ NÁSTROJŮ (Default Elements)
 * ============================================================================ */

static UInt64 MakeElemTypeKey (const API_ElemType& t)
{
	return (static_cast<UInt64> (static_cast<UInt32> (t.typeID)) << 32) |
		   static_cast<UInt32> (t.variationID);
}

/* Pouze skutečné primární nástroje z palety nástrojů Archicadu (které uživatel může aktivovat).
 * Nezahrnuje interní pod-komponenty (Segment trámu/sloupu, Stupnice, Podstupnice, prvky zábradlí/LOP)
 * ani analytické vazby, které nemají samostatné výchozí nastavení nástroje. */
static void CollectPrimaryToolboxTools (GS::Array<API_ElemType>& outTypes)
{
	outTypes.Clear ();
	const API_ElemTypeID mainTools[] = {
		API_WallID, API_ColumnID, API_BeamID, API_WindowID, API_DoorID,
		API_ObjectID, API_LampID, API_SlabID, API_RoofID, API_MeshID,
		API_ZoneID, API_CurtainWallID, API_ShellID, API_SkylightID,
		API_MorphID, API_StairID, API_RailingID, API_OpeningID,
		API_DimensionID, API_RadialDimensionID, API_LevelDimensionID, API_AngleDimensionID,
		API_TextID, API_LabelID, API_HatchID, API_LineID, API_PolyLineID,
		API_ArcID, API_SplineID, API_HotspotID,
		API_CutPlaneID, API_ElevationID, API_InteriorElevationID,
		API_DetailID, API_WorksheetID, API_PictureID, API_DrawingID, API_ChangeMarkerID
	};
	for (API_ElemTypeID id : mainTools) {
		outTypes.Push (API_ElemType (id, APIVarId_Generic));
	}
}

static void CollectFavoriteCandidateTypes (GS::Array<API_ElemType>& outTypes)
{
	outTypes.Clear ();
	GS::HashSet<UInt64> seen;

	auto addType = [&] (const API_ElemType& t) {
		if (t.typeID == API_ZombieElemID) {
			return;
		}
		const UInt64 k = MakeElemTypeKey (t);
		if (!seen.Contains (k)) {
			seen.Add (k);
			outTypes.Push (t);
		}
	};

	API_ToolBoxInfo toolboxInfo = {};
	if (ACAPI_Toolbox_GetToolBoxInfo (&toolboxInfo, true) == NoError && toolboxInfo.data != nullptr) {
		for (Int32 i = 0; i < toolboxInfo.nTools; ++i) {
			addType ((*toolboxInfo.data)[i].type);
		}
	}
	if (toolboxInfo.data != nullptr) {
		BMKillHandle (reinterpret_cast<GSHandle*> (&toolboxInfo.data));
	}

	GS::Array<API_ElemType> primaryTools;
	CollectPrimaryToolboxTools (primaryTools);
	for (const API_ElemType& t : primaryTools) {
		addType (t);
	}

	const API_ElemTypeID subElemTypes[] = {
		API_CircleID,
		API_BeamSegmentID, API_ColumnSegmentID,
		API_CurtainWallSegmentID, API_CurtainWallFrameID, API_CurtainWallPanelID,
		API_CurtainWallJunctionID, API_CurtainWallAccessoryID,
		API_RiserID, API_TreadID, API_StairStructureID,
		API_RailingPostID, API_RailingToprailID, API_RailingHandrailID,
		API_RailingRailID, API_RailingInnerPostID, API_RailingBalusterID,
		API_RailingPanelID, API_RailingSegmentID
	};
	for (API_ElemTypeID id : subElemTypes) {
		addType (API_ElemType (id, APIVarId_Generic));
	}
}

static bool IsMarkeredDefaultElemType (API_ElemTypeID typeID)
{
	switch (typeID) {
		case API_WindowID:
		case API_DoorID:
		case API_SkylightID:
		case API_CutPlaneID:
		case API_ElevationID:
		case API_InteriorElevationID:
		case API_DetailID:
		case API_WorksheetID:
		case API_ChangeMarkerID:
			return true;
		default:
			return false;
	}
}

static bool LoadToolDefaultElement (
	const API_ElemType&	toolType,
	bool				withMemo,
	API_Element&		outElem,
	API_ElementMemo&	outMemo)
{
	BNZeroMemory (&outElem, sizeof (API_Element));
	BNZeroMemory (&outMemo, sizeof (API_ElementMemo));
	outElem.header.type = toolType;

	API_ElementMemo* memoPtr = withMemo ? &outMemo : nullptr;
	if (IsMarkeredDefaultElemType (toolType.typeID)) {
		if (ACAPI_Element_GetDefaultsExt (&outElem, memoPtr, 0UL, nullptr) == NoError) {
			outElem.header.type = toolType;
			return true;
		}
		if (withMemo) {
			ACAPI_DisposeElemMemoHdls (&outMemo);
		}
		BNZeroMemory (&outElem, sizeof (API_Element));
		BNZeroMemory (&outMemo, sizeof (API_ElementMemo));
		outElem.header.type = toolType;
	}
	if (ACAPI_Element_GetDefaults (&outElem, memoPtr) == NoError) {
		outElem.header.type = toolType;
		return true;
	}
	if (withMemo) {
		ACAPI_DisposeElemMemoHdls (&outMemo);
	}
	if (!IsMarkeredDefaultElemType (toolType.typeID)) {
		BNZeroMemory (&outElem, sizeof (API_Element));
		BNZeroMemory (&outMemo, sizeof (API_ElementMemo));
		outElem.header.type = toolType;
		if (ACAPI_Element_GetDefaultsExt (&outElem, memoPtr, 0UL, nullptr) == NoError) {
			outElem.header.type = toolType;
			return true;
		}
		if (withMemo) {
			ACAPI_DisposeElemMemoHdls (&outMemo);
		}
	}
	BNZeroMemory (&outMemo, sizeof (API_ElementMemo));
	return false;
}

static void CheckMissingAttributesInDefaults (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::HashTable<UInt64, bool> existsCache;
	GS::HashTable<Int32, bool>  libPartCache;

	GS::Array<API_ElemType> toolTypes;
	CollectPrimaryToolboxTools (toolTypes);

	for (const API_ElemType& toolType : toolTypes) {
		API_Element     elem;
		API_ElementMemo memo;
		if (!LoadToolDefaultElement (toolType, true, elem, memo)) {
			continue;
		}
		bool is3DCapable = false;
		const UInt32 caps = GetElementCapabilities (elem, libPartCache, is3DCapable, false);
		if (caps == kElemCap_None) {
			ACAPI_DisposeElemMemoHdls (&memo);
			continue;
		}

		const GS::UniString toolName = GetElemTypeName (toolType);
		InspectElementMissingAttributes (elem, &memo, caps, existsCache,
			[&] (API_AttrTypeID refAttrType, API_AttributeIndex refAttrIndex, const GS::UniString& usageRole) {
				AuditRow row;
				row.toolType  = toolType;
				row.attrType  = refAttrType;
				row.attrIndex = refAttrIndex;
				row.cells.Push (toolName);
				row.cells.Push (GetAttrTypeNameCZ (refAttrType));
				row.cells.Push (usageRole);
				inOutState.rows.Push (row);
			});

		ACAPI_DisposeElemMemoHdls (&memo);
	}
}

static void CheckUnclassifiedDefaults (
	const GS::HashSet<API_Guid>&	disabledClassificationSystems,
	SubCheckState&					inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::Array<API_ClassificationSystem> allSystems;
	if (ACAPI_Classification_GetClassificationSystems (allSystems) != NoError || allSystems.IsEmpty ()) {
		return;
	}

	GS::Array<API_ClassificationSystem> activeSystems;
	for (const API_ClassificationSystem& sys : allSystems) {
		if (!disabledClassificationSystems.Contains (sys.guid)) {
			activeSystems.Push (sys);
		}
	}
	if (activeSystems.IsEmpty ()) {
		return;
	}

	GS::HashTable<Int32, GS::UniString> layerCache;
	GS::HashTable<Int32, bool>          libPartCache;

	GS::Array<API_ElemType> toolTypes;
	CollectPrimaryToolboxTools (toolTypes);

	for (const API_ElemType& toolType : toolTypes) {
		API_Element     elem;
		API_ElementMemo memo;
		if (!LoadToolDefaultElement (toolType, false, elem, memo)) {
			continue;
		}
		bool is3DCapable = false;
		const UInt32 caps = GetElementCapabilities (elem, libPartCache, is3DCapable, false);
		if ((caps & kElemCap_Classification) == 0) {
			continue;
		}

		GS::Array<GS::Pair<API_Guid, API_Guid>> sysItemPairs;
		if (ACAPI_Element_GetClassificationItemsDefault (toolType, sysItemPairs) != NoError) {
			continue;
		}

		const GS::UniString missingSystemsStr = FormatMissingClassificationsList (activeSystems, sysItemPairs);
		if (!missingSystemsStr.IsEmpty ()) {
			AuditRow row;
			row.toolType = toolType;
			row.cells.Push (GetElemTypeName (toolType));
			row.cells.Push (LookupLayerName (elem, caps, layerCache));
			row.cells.Push (missingSystemsStr);
			inOutState.rows.Push (row);
		}
	}
}

static void CheckUndefinedCategoryDefault (
	API_ElemCategoryID	categoryID,
	UInt32				requiredCapBit,
	const char*			undefGuidStr,
	SubCheckState&		inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	const API_Guid undefValueGuid = APIGuidFromString (undefGuidStr);

	API_ElemCategory elemCategory;
	BNZeroMemory (&elemCategory, sizeof (API_ElemCategory));
	elemCategory.categoryID = categoryID;

	GS::HashTable<Int32, GS::UniString> layerCache;
	GS::HashTable<Int32, bool>          libPartCache;

	GS::Array<API_ElemType> toolTypes;
	CollectPrimaryToolboxTools (toolTypes);

	for (const API_ElemType& toolType : toolTypes) {
		API_Element     elem;
		API_ElementMemo memo;
		if (!LoadToolDefaultElement (toolType, false, elem, memo)) {
			continue;
		}
		bool is3DCapable = false;
		const UInt32 caps = GetElementCapabilities (elem, libPartCache, is3DCapable, false);
		if ((caps & requiredCapBit) == 0) {
			continue;
		}

		API_ElemCategoryValue catVal;
		BNZeroMemory (&catVal, sizeof (API_ElemCategoryValue));
		if (ACAPI_Category_GetCategoryValueDefault (toolType, elemCategory, &catVal) != NoError) {
			continue;
		}

		if (catVal.guid == undefValueGuid || catVal.guid == APINULLGuid) {
			AuditRow row;
			row.toolType = toolType;
			row.cells.Push (GetElemTypeName (toolType));
			row.cells.Push (LookupLayerName (elem, caps, layerCache));
			inOutState.rows.Push (row);
		}
	}
}

static void CheckDefaultsOnArchicadLayer (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::HashTable<Int32, bool> libPartCache;

	GS::Array<API_ElemType> toolTypes;
	CollectPrimaryToolboxTools (toolTypes);

	for (const API_ElemType& toolType : toolTypes) {
		API_Element     elem;
		API_ElementMemo memo;
		if (!LoadToolDefaultElement (toolType, false, elem, memo)) {
			continue;
		}
		bool is3DCapable = false;
		const UInt32 caps = GetElementCapabilities (elem, libPartCache, is3DCapable, false);
		if ((caps & kElemCap_Layer) == 0) {
			continue;
		}

		if (elem.header.layer == APIApplicationLayerAttributeIndex) {
			AuditRow row;
			row.toolType = toolType;
			row.cells.Push (GetElemTypeName (toolType));
			inOutState.rows.Push (row);
		}
	}
}

/* ============================================================================
 * 5: OBLÍBENÁ NASTAVENÍ (Favorites)
 * ============================================================================ */

struct FavoriteDescriptor {
	GS::UniString	name;
	API_ElemType	toolType;
	GS::UniString	folderPath;
};

static GS::UniString FormatFavoriteFolder (const API_FavoriteFolderHierarchy& folder)
{
	if (folder.IsEmpty ()) {
		return GS::UniString (u"—");
	}
	GS::UniString pathStr;
	for (UIndex i = 0; i < folder.GetSize (); ++i) {
		if (folder[i].IsEmpty ()) {
			continue;
		}
		if (!pathStr.IsEmpty ()) {
			pathStr += GS::UniString (" / ");
		}
		pathStr += folder[i];
	}
	if (pathStr.IsEmpty ()) {
		return GS::UniString (u"—");
	}
	return pathStr;
}

static void CollectAllProjectFavorites (GS::Array<FavoriteDescriptor>& outFavorites)
{
	outFavorites.Clear ();
	GS::HashSet<GS::UniString> seenNames;

	GS::Array<API_ElemType> candidateTypes;
	CollectFavoriteCandidateTypes (candidateTypes);

	for (const API_ElemType& toolType : candidateTypes) {
		short count = 0;
		GS::Array<API_FavoriteFolderHierarchy> folders;
		GS::Array<GS::UniString>               names;
		if (ACAPI_Favorite_GetNum (toolType, &count, &folders, &names) != NoError || count <= 0) {
			continue;
		}
		for (UIndex i = 0; i < names.GetSize (); ++i) {
			const GS::UniString& favName = names[i];
			if (favName.IsEmpty () || seenNames.Contains (favName)) {
				continue;
			}
			seenNames.Add (favName);

			FavoriteDescriptor desc;
			desc.name     = favName;
			desc.toolType = toolType;
			if (i < folders.GetSize ()) {
				desc.folderPath = FormatFavoriteFolder (folders[i]);
			} else {
				desc.folderPath = GS::UniString (u"—");
			}
			outFavorites.Push (desc);
		}
	}
}

static bool FavoriteNeedsMemoForAttrCheck (API_ElemTypeID typeID)
{
	switch (typeID) {
		case API_BeamID:
		case API_ColumnID:
		case API_ObjectID:
		case API_LampID:
		case API_WindowID:
		case API_DoorID:
		case API_SkylightID:
		case API_ZoneID:
			return true;
		default:
			return false;
	}
}

static void CheckMissingAttributesInFavorites (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::HashTable<UInt64, bool> existsCache;
	GS::HashTable<Int32, bool>  libPartCache;

	GS::Array<FavoriteDescriptor> favorites;
	CollectAllProjectFavorites (favorites);

	for (const FavoriteDescriptor& desc : favorites) {
		API_Favorite fav (desc.name);
		BNZeroMemory (&fav.element, sizeof (API_Element));
		const bool requestMemo = FavoriteNeedsMemoForAttrCheck (desc.toolType.typeID);
		if (requestMemo) {
			fav.memo.New ();
			BNZeroMemory (&fav.memo.Get (), sizeof (API_ElementMemo));
		}

		if (ACAPI_Favorite_Get (&fav) != NoError) {
			if (fav.memo.HasValue ()) {
				ACAPI_DisposeElemMemoHdls (&fav.memo.Get ());
			}
			continue;
		}
		if (fav.element.header.type.typeID == API_ZombieElemID) {
			fav.element.header.type = desc.toolType;
		}

		bool is3DCapable = false;
		const UInt32 caps = GetElementCapabilities (fav.element, libPartCache, is3DCapable, false);
		const GS::UniString toolName = GetElemTypeName (fav.element.header.type);
		const API_ElementMemo* memoPtr = fav.memo.HasValue () ? &fav.memo.Get () : nullptr;

		InspectElementMissingAttributes (fav.element, memoPtr, caps, existsCache,
			[&] (API_AttrTypeID refAttrType, API_AttributeIndex refAttrIndex, const GS::UniString& usageRole) {
				AuditRow row;
				row.toolType     = fav.element.header.type;
				row.favoriteName = desc.name;
				row.attrType     = refAttrType;
				row.attrIndex    = refAttrIndex;
				row.cells.Push (desc.name);
				row.cells.Push (toolName);
				row.cells.Push (desc.folderPath);
				row.cells.Push (GetAttrTypeNameCZ (refAttrType));
				row.cells.Push (usageRole);
				inOutState.rows.Push (row);
			});

		if (fav.memo.HasValue ()) {
			ACAPI_DisposeElemMemoHdls (&fav.memo.Get ());
		}
	}
}

static void CheckUnclassifiedFavorites (
	const GS::HashSet<API_Guid>&	disabledClassificationSystems,
	SubCheckState&					inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::Array<API_ClassificationSystem> allSystems;
	if (ACAPI_Classification_GetClassificationSystems (allSystems) != NoError || allSystems.IsEmpty ()) {
		return;
	}

	GS::Array<API_ClassificationSystem> activeSystems;
	for (const API_ClassificationSystem& sys : allSystems) {
		if (!disabledClassificationSystems.Contains (sys.guid)) {
			activeSystems.Push (sys);
		}
	}
	if (activeSystems.IsEmpty ()) {
		return;
	}

	GS::HashTable<Int32, GS::UniString> layerCache;
	GS::HashTable<Int32, bool>          libPartCache;

	GS::Array<FavoriteDescriptor> favorites;
	CollectAllProjectFavorites (favorites);

	for (const FavoriteDescriptor& desc : favorites) {
		API_Favorite fav (desc.name);
		BNZeroMemory (&fav.element, sizeof (API_Element));
		fav.classifications.New ();

		if (ACAPI_Favorite_Get (&fav) != NoError) {
			continue;
		}
		if (fav.element.header.type.typeID == API_ZombieElemID) {
			fav.element.header.type = desc.toolType;
		}

		bool is3DCapable = false;
		const UInt32 caps = GetElementCapabilities (fav.element, libPartCache, is3DCapable, false);
		if ((caps & kElemCap_Classification) == 0) {
			continue;
		}

		const GS::Array<GS::Pair<API_Guid, API_Guid>> emptyPairs;
		const GS::Array<GS::Pair<API_Guid, API_Guid>>& sysItemPairs =
			fav.classifications.HasValue () ? fav.classifications.Get () : emptyPairs;

		const GS::UniString missingSystemsStr = FormatMissingClassificationsList (activeSystems, sysItemPairs);
		if (!missingSystemsStr.IsEmpty ()) {
			AuditRow row;
			row.toolType     = fav.element.header.type;
			row.favoriteName = desc.name;
			row.cells.Push (desc.name);
			row.cells.Push (GetElemTypeName (fav.element.header.type));
			row.cells.Push (desc.folderPath);
			row.cells.Push (LookupLayerName (fav.element, caps, layerCache));
			row.cells.Push (missingSystemsStr);
			inOutState.rows.Push (row);
		}
	}
}

static void CheckUndefinedCategoryFavorite (
	API_ElemCategoryID	categoryID,
	UInt32				requiredCapBit,
	const char*			undefGuidStr,
	SubCheckState&		inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	const API_Guid undefValueGuid = APIGuidFromString (undefGuidStr);

	API_Guid targetCategoryGuid = APINULLGuid;
	GS::Array<API_ElemCategory> categoryList;
	if (ACAPI_Category_GetElementCategories (&categoryList) == NoError) {
		for (const API_ElemCategory& cat : categoryList) {
			if (cat.categoryID == categoryID) {
				targetCategoryGuid = cat.guid;
				break;
			}
		}
	}

	GS::HashTable<Int32, GS::UniString> layerCache;
	GS::HashTable<Int32, bool>          libPartCache;

	GS::Array<FavoriteDescriptor> favorites;
	CollectAllProjectFavorites (favorites);

	for (const FavoriteDescriptor& desc : favorites) {
		API_Favorite fav (desc.name);
		BNZeroMemory (&fav.element, sizeof (API_Element));
		fav.elemCategoryValues.New ();

		if (ACAPI_Favorite_Get (&fav) != NoError) {
			continue;
		}
		if (fav.element.header.type.typeID == API_ZombieElemID) {
			fav.element.header.type = desc.toolType;
		}

		bool is3DCapable = false;
		const UInt32 caps = GetElementCapabilities (fav.element, libPartCache, is3DCapable, false);
		if ((caps & requiredCapBit) == 0) {
			continue;
		}

		bool isUndefined = true;

		if (fav.elemCategoryValues.HasValue ()) {
			for (const API_ElemCategoryValue& val : fav.elemCategoryValues.Get ()) {
				const bool matchesCat =
					(val.category.categoryID == categoryID) ||
					(targetCategoryGuid != APINULLGuid && val.category.guid == targetCategoryGuid);
				if (matchesCat) {
					if (val.guid != undefValueGuid && val.guid != APINULLGuid) {
						isUndefined = false;
					}
					break;
				}
			}
		}

		if (isUndefined) {
			AuditRow row;
			row.toolType     = fav.element.header.type;
			row.favoriteName = desc.name;
			row.cells.Push (desc.name);
			row.cells.Push (GetElemTypeName (fav.element.header.type));
			row.cells.Push (desc.folderPath);
			row.cells.Push (LookupLayerName (fav.element, caps, layerCache));
			inOutState.rows.Push (row);
		}
	}
}

static void CheckFavoritesOnArchicadLayer (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::HashTable<Int32, bool> libPartCache;

	GS::Array<FavoriteDescriptor> favorites;
	CollectAllProjectFavorites (favorites);

	for (const FavoriteDescriptor& desc : favorites) {
		API_Favorite fav (desc.name);
		BNZeroMemory (&fav.element, sizeof (API_Element));

		if (ACAPI_Favorite_Get (&fav) != NoError) {
			continue;
		}
		if (fav.element.header.type.typeID == API_ZombieElemID) {
			fav.element.header.type = desc.toolType;
		}

		bool is3DCapable = false;
		const UInt32 caps = GetElementCapabilities (fav.element, libPartCache, is3DCapable, false);
		if ((caps & kElemCap_Layer) == 0) {
			continue;
		}

		if (fav.element.header.layer == APIApplicationLayerAttributeIndex) {
			AuditRow row;
			row.toolType     = fav.element.header.type;
			row.favoriteName = desc.name;
			row.cells.Push (desc.name);
			row.cells.Push (GetElemTypeName (fav.element.header.type));
			row.cells.Push (desc.folderPath);
			inOutState.rows.Push (row);
		}
	}
}

/* ============================================================================
 * 2: VLASTNOSTI (Properties)
 * ============================================================================ */

static GS::UniString GetPropertyDataTypeNameCZ (const API_PropertyDefinition& defn)
{
	GS::UniString baseType;
	switch (defn.valueType) {
		case API_PropertyIntegerValueType:	baseType = GS::UniString (u"Celé číslo"); break;
		case API_PropertyRealValueType:		baseType = GS::UniString (u"Číslo"); break;
		case API_PropertyStringValueType:	baseType = GS::UniString (u"Řetězec"); break;
		case API_PropertyBooleanValueType:	baseType = GS::UniString (u"Logická hodnota ( Pravda/Nepravda )"); break;
		case API_PropertyGuidValueType:		baseType = GS::UniString (u"GUID"); break;
		default:							baseType = GS::UniString (u"Obecný typ"); break;
	}
	switch (defn.collectionType) {
		case API_PropertyListCollectionType:
			return baseType + GS::UniString (u" (Seznam značek)");
		case API_PropertySingleChoiceEnumerationCollectionType:
			return GS::UniString (u"Sada voleb");
		case API_PropertyMultipleChoiceEnumerationCollectionType:
			return GS::UniString (u"Sada voleb (vícenásobný výběr)");
		case API_PropertySingleCollectionType:
		default:
			return baseType;
	}
}

static void CheckUnassignedProperties (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::Array<API_PropertyGroup> groups;
	if (ACAPI_Property_GetPropertyGroups (groups) != NoError) {
		return;
	}

	GS::HashTable<API_Guid, bool> validClassItemCache;

	for (const API_PropertyGroup& group : groups) {
		if (group.groupType != API_PropertyCustomGroupType) {
			continue;
		}
		GS::Array<API_PropertyDefinition> defs;
		if (ACAPI_Property_GetPropertyDefinitions (group.guid, defs) != NoError) {
			continue;
		}
		for (const API_PropertyDefinition& defn : defs) {
			if (defn.definitionType != API_PropertyCustomDefinitionType) {
				continue;
			}
			bool hasValidClass = false;
			for (const API_Guid& classGuid : defn.availability) {
				if (classGuid == APINULLGuid) {
					continue;
				}
				if (validClassItemCache.ContainsKey (classGuid)) {
					if (validClassItemCache[classGuid]) {
						hasValidClass = true;
						break;
					}
					continue;
				}
				API_ClassificationItem item = {};
				item.guid = classGuid;
				const bool ok = (ACAPI_Classification_GetClassificationItem (item) == NoError);
				validClassItemCache.Put (classGuid, ok);
				if (ok) {
					hasValidClass = true;
					break;
				}
			}

			if (!hasValidClass) {
				AuditRow row;
				row.cells.Push (group.name);
				row.cells.Push (defn.name);
				row.cells.Push (GetPropertyDataTypeNameCZ (defn));
				inOutState.rows.Push (row);
			}
		}
	}
}

/* ============================================================================
 * 6: ZOBRAZENÍ (Views — Nepoužitá nastavení zobrazení v mapě zobrazení)
 * ============================================================================ */

struct UsedViewSettingsCache {
	GS::HashSet<GS::UniString>		usedPenSetNames;
	GS::HashSet<Int32>				usedPenSetIndices;
	GS::HashSet<GS::UniString>		usedLayerCombNames;
	GS::HashSet<Int32>				usedLayerCombIndices;
	GS::HashSet<API_Guid>			usedRenovationFilterGuids;
	GS::HashSet<GS::UniString>		used3DStyleNames;
	GS::HashSet<GS::UniString>		usedGraphicOverrideNames;
	GS::HashSet<GS::UniString>		usedMVONames;
};

static void DisposeNavigatorViewData (API_NavigatorView& navView)
{
	if (navView.modelViewOpt != nullptr) {
		ACAPI_FreeGDLModelViewOptionsPtr (&navView.modelViewOpt->gdlOptions);
		BMKillPtr (reinterpret_cast<GSPtr*> (&navView.modelViewOpt));
	}
	if (navView.dimPrefs != nullptr) {
		BMKillPtr (reinterpret_cast<GSPtr*> (&navView.dimPrefs));
	}
	if (navView.layerStats != nullptr) {
		delete navView.layerStats;
		navView.layerStats = nullptr;
	}
	if (navView.pens != nullptr) {
		BMKillPtr (reinterpret_cast<GSPtr*> (&navView.pens));
	}
}

static void AddCStringBothEncodings (const char* str, GS::HashSet<GS::UniString>& outSet)
{
	if (str == nullptr || str[0] == '\0') {
		return;
	}
	const GS::UniString uUtf8 (str, CC_UTF8);
	if (!uUtf8.IsEmpty ()) {
		outSet.Add (uUtf8);
	}
	const GS::UniString uDef (str, CC_Default);
	if (!uDef.IsEmpty ()) {
		outSet.Add (uDef);
	}
}

static void TraverseNavigatorViewNode (
	const API_NavigatorItem&	parentItem,
	UsedViewSettingsCache&		inOutUsed,
	int							depth)
{
	if (depth > 64) {
		return;
	}
	GS::Array<API_NavigatorItem> children;
	API_NavigatorItem parentCopy = parentItem;
	if (ACAPI_Navigator_GetNavigatorChildrenItems (&parentCopy, &children) != NoError) {
		return;
	}

	for (UIndex i = 0; i < children.GetSize (); ++i) {
		API_NavigatorItem& child = children[i];
		child.mapId = parentItem.mapId;

		if (child.itemType != API_UndefinedNavItem &&
			child.itemType != API_ProjectNavItem &&
			child.itemType != API_FolderNavItem) {
			API_NavigatorView navView;
			BNZeroMemory (&navView, sizeof (API_NavigatorView));
			if (ACAPI_Navigator_GetNavigatorView (&child, &navView) == NoError) {
				if (navView.savePenSet && navView.penSetName[0] != '\0') {
					AddCStringBothEncodings (navView.penSetName, inOutUsed.usedPenSetNames);
				}
				if (navView.saveLaySet && navView.layerCombination[0] != '\0') {
					AddCStringBothEncodings (navView.layerCombination, inOutUsed.usedLayerCombNames);
				}
				if (navView.renovationFilterGuid != APINULLGuid) {
					inOutUsed.usedRenovationFilterGuids.Add (navView.renovationFilterGuid);
				}
				if (GS::ucslen (navView.d3styleName) > 0) {
					inOutUsed.used3DStyleNames.Add (GS::UniString (navView.d3styleName));
				}
				if (GS::ucslen (navView.overrideCombination) > 0) {
					inOutUsed.usedGraphicOverrideNames.Add (GS::UniString (navView.overrideCombination));
				}
				if (navView.saveDispOpt && navView.modelViewOptName[0] != '\0') {
					AddCStringBothEncodings (navView.modelViewOptName, inOutUsed.usedMVONames);
				}
			}
			DisposeNavigatorViewData (navView);
		}

		TraverseNavigatorViewNode (child, inOutUsed, depth + 1);
	}
}

static void CollectUsedViewSettings (UsedViewSettingsCache& outUsed)
{
	const API_NavigatorMapID viewMaps[] = { API_PublicViewMap, API_MyViewMap };
	for (API_NavigatorMapID mapId : viewMaps) {
		Int32 setCount = 0;
		if (ACAPI_Navigator_GetNavigatorSetNum (&setCount) != NoError) {
			setCount = 1;
		}
		API_NavigatorSet navSet;
		BNZeroMemory (&navSet, sizeof (API_NavigatorSet));
		navSet.mapId          = mapId;
		navSet.wantsExtraInfo = false;
		if (ACAPI_Navigator_GetNavigatorSet (&navSet) == NoError && navSet.rootGuid != APINULLGuid) {
			API_NavigatorItem rootItem;
			BNZeroMemory (&rootItem, sizeof (API_NavigatorItem));
			rootItem.guid  = navSet.rootGuid;
			rootItem.mapId = mapId;
			TraverseNavigatorViewNode (rootItem, outUsed, 0);
		}
	}

	/* Sady per použité přímo na vložených kresbách (API_DrawingID) */
	GS::Array<API_Guid> drawingGuids;
	if (ACAPI_Element_GetElemList (API_DrawingID, &drawingGuids) == NoError) {
		for (const API_Guid& dGuid : drawingGuids) {
			API_Element dElem;
			BNZeroMemory (&dElem, sizeof (API_Element));
			dElem.header.guid = dGuid;
			if (ACAPI_Element_Get (&dElem) == NoError) {
				if (dElem.drawing.penTableUsageMode == APIPenTableUsageMode_UsePenTableIndex &&
					dElem.drawing.penTableIndex.ToInt32_Deprecated () > 0) {
					outUsed.usedPenSetIndices.Add (dElem.drawing.penTableIndex.ToInt32_Deprecated ());
				}
			}
		}
	}

	/* Aktuálně aktivní kombinace vrstev v okně */
	API_AttributeIndex currLayComb = APIInvalidAttributeIndex;
	if (ACAPI_Navigator_GetCurrLayerComb (&currLayComb) == NoError && currLayComb.ToInt32_Deprecated () > 0) {
		outUsed.usedLayerCombIndices.Add (currLayComb.ToInt32_Deprecated ());
	}
}

static GS::UniString ReadAttrHeaderName (const API_Attribute& attr, const GS::UniString& uName)
{
	if (!uName.IsEmpty ()) {
		return uName;
	}
	return GS::UniString (attr.header.name, CC_Default);
}

static void CheckUnusedPenTables (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	UsedViewSettingsCache used;
	CollectUsedViewSettings (used);

	UInt32 count = 0;
	if (ACAPI_Attribute_GetNum (API_PenTableID, count) != NoError) {
		return;
	}
	const Int32 maxInd = static_cast<Int32> (count);
	for (Int32 i = 1; i <= maxInd; ++i) {
		API_Attribute attr;
		BNZeroMemory (&attr, sizeof (API_Attribute));
		GS::UniString uName;
		attr.header.typeID           = API_PenTableID;
		attr.header.index            = ACAPI_CreateAttributeIndex (i);
		attr.header.uniStringNamePtr = &uName;
		if (ACAPI_Attribute_Get (&attr) != NoError) {
			continue;
		}
		const GS::UniString name = ReadAttrHeaderName (attr, uName);
		if (used.usedPenSetIndices.Contains (i) || used.usedPenSetNames.Contains (name)) {
			continue;
		}
		AuditRow row;
		row.attrType  = API_PenTableID;
		row.attrIndex = attr.header.index;
		row.cells.Push (name);
		row.cells.Push (GS::UniString::Printf ("%d", (int) i));
		inOutState.rows.Push (row);
	}
}

static void CheckUnusedLayerCombinations (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	UsedViewSettingsCache used;
	CollectUsedViewSettings (used);

	UInt32 count = 0;
	if (ACAPI_Attribute_GetNum (API_LayerCombID, count) != NoError) {
		return;
	}
	const Int32 maxInd = static_cast<Int32> (count);
	for (Int32 i = 1; i <= maxInd; ++i) {
		API_Attribute attr;
		BNZeroMemory (&attr, sizeof (API_Attribute));
		GS::UniString uName;
		attr.header.typeID           = API_LayerCombID;
		attr.header.index            = ACAPI_CreateAttributeIndex (i);
		attr.header.uniStringNamePtr = &uName;
		if (ACAPI_Attribute_Get (&attr) != NoError) {
			continue;
		}
		const GS::UniString name = ReadAttrHeaderName (attr, uName);
		if (used.usedLayerCombIndices.Contains (i) || used.usedLayerCombNames.Contains (name)) {
			continue;
		}
		AuditRow row;
		row.attrType  = API_LayerCombID;
		row.attrIndex = attr.header.index;
		row.cells.Push (name);
		row.cells.Push (GS::UniString::Printf ("%d", (int) i));
		inOutState.rows.Push (row);
	}
}

static void CheckUnusedRenovationFilters (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	UsedViewSettingsCache used;
	CollectUsedViewSettings (used);

	GS::Array<API_Guid> filterGuids;
	if (ACAPI_Renovation_GetRenovationFilters (&filterGuids) != NoError) {
		return;
	}
	for (const API_Guid& filterGuid : filterGuids) {
		if (used.usedRenovationFilterGuids.Contains (filterGuid)) {
			continue;
		}
		GS::UniString filterName;
		if (ACAPI_Renovation_GetRenovationFilterName (&filterGuid, &filterName) != NoError || filterName.IsEmpty ()) {
			filterName = APIGuidToString (filterGuid);
		}
		AuditRow row;
		row.cells.Push (filterName);
		inOutState.rows.Push (row);
	}
}

static void CheckUnused3DStyles (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	UsedViewSettingsCache used;
	CollectUsedViewSettings (used);

	GS::Array<GS::UniString> styleNames;
	GS::UniString            currentStyle;
	if (ACAPI_View_Get3DStyleList (&styleNames, &currentStyle) != NoError) {
		return;
	}
	if (!currentStyle.IsEmpty ()) {
		used.used3DStyleNames.Add (currentStyle);
	}
	for (const GS::UniString& styleName : styleNames) {
		if (used.used3DStyleNames.Contains (styleName)) {
			continue;
		}
		AuditRow row;
		row.cells.Push (styleName);
		inOutState.rows.Push (row);
	}
}

static void CheckUnusedGraphicOverrides (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	UsedViewSettingsCache used;
	CollectUsedViewSettings (used);

	GS::Array<API_Guid> combGuids;
	if (ACAPI_GraphicalOverride_GetOverrideCombinationList (combGuids) != NoError) {
		return;
	}
	for (const API_Guid& combGuid : combGuids) {
		API_OverrideCombination comb = {};
		comb.guid = combGuid;
		if (ACAPI_GraphicalOverride_GetOverrideCombination (comb) != NoError) {
			continue;
		}
		if (used.usedGraphicOverrideNames.Contains (comb.name)) {
			continue;
		}
		AuditRow row;
		row.cells.Push (comb.name);
		inOutState.rows.Push (row);
	}
}

static void CheckUnusedModelViewOptions (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	UsedViewSettingsCache used;
	CollectUsedViewSettings (used);

	UInt32 count = 0;
	ACAPI_Navigator_ModelViewOptions_GetNum (count);
	if (count == 0) {
		return;
	}
	for (UInt32 i = 1; i <= count; ++i) {
		API_ModelViewOptionsType mvoType;
		BNZeroMemory (&mvoType, sizeof (API_ModelViewOptionsType));
		GS::UniString uName;
		mvoType.head.index            = i;
		mvoType.head.uniStringNamePtr = &uName;
		if (ACAPI_Navigator_ModelViewOptions_Get (&mvoType) != NoError) {
			continue;
		}
		if (mvoType.modelViewOpt.gdlOptions != nullptr) {
			ACAPI_FreeGDLModelViewOptionsPtr (&mvoType.modelViewOpt.gdlOptions);
		}
		const GS::UniString name = uName.IsEmpty () ? GS::UniString (mvoType.head.name, CC_Default) : uName;
		if (used.usedMVONames.Contains (name)) {
			continue;
		}
		AuditRow row;
		row.attrType  = API_ZombieAttrID;
		row.attrIndex = ACAPI_CreateAttributeIndex (static_cast<Int32> (i));
		row.cells.Push (name);
		row.cells.Push (GS::UniString::Printf ("%u", (unsigned int) i));
		inOutState.rows.Push (row);
	}
}

/* ============================================================================
 * 7: DOKUMENTACE (Docs — Neasociované popisky a Kóty bez naměřené hodnoty)
 * ============================================================================ */

static GS::UniString GetLabelClassNameCZ (API_LblClassID lblClass)
{
	switch (lblClass) {
		case APILblClass_Text:
			return GS::UniString (u"Popiska (Text / Autotext)");
		case APILblClass_Symbol:
			return GS::UniString (u"Popiska (Symbol)");
		default:
			return GS::UniString (u"Popiska");
	}
}

static void CheckUnassociatedLabels (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::HashTable<short, GS::UniString> storyMap;
	BuildStoryNameMap (storyMap);
	GS::HashTable<Int32, GS::UniString> layerCache;

	GS::Array<API_Guid> labelGuids;
	if (ACAPI_Element_GetElemList (API_LabelID, &labelGuids) != NoError) {
		return;
	}

	for (const API_Guid& guid : labelGuids) {
		API_Element elem;
		BNZeroMemory (&elem, sizeof (API_Element));
		elem.header.guid = guid;
		if (ACAPI_Element_Get (&elem) != NoError) {
			continue;
		}
		if (elem.label.parent != APINULLGuid) {
			continue;
		}

		AuditRow row;
		row.elemGuid    = guid;
		row.is3DCapable = false;
		row.cells.Push (GetLabelClassNameCZ (elem.label.labelClass));
		row.cells.Push (LookupStoryName (elem.header.floorInd, storyMap));
		row.cells.Push (LookupLayerName (elem, kElemCap_Layer, layerCache));
		inOutState.rows.Push (row);
	}
}

static const GS::Array<GS::UniString>& GetDimensionAutotextKeywordsLower (void)
{
	static GS::Array<GS::UniString> sKeywordsLower;
	static bool sInitialized = false;
	if (!sInitialized) {
		sInitialized = true;
		const API_NoteAutotextID kAllNoteAutotexts[] = {
			API_NoteAutotextID::MeasuredValue,
			API_NoteAutotextID::MeasuredValueToPZ,
			API_NoteAutotextID::MeasuredValueToFirstRL,
			API_NoteAutotextID::MeasuredValueToSecondRL,
			API_NoteAutotextID::MeasuredValueToAltitude,
			API_NoteAutotextID::MeasuredValueToSO,
			API_NoteAutotextID::StoredOriginToPZ,
			API_NoteAutotextID::TopElevToPZ,
			API_NoteAutotextID::TopElevToFirstRL,
			API_NoteAutotextID::TopElevToSecondRL,
			API_NoteAutotextID::TopElevToAltitude,
			API_NoteAutotextID::CoreTopElevToPZ,
			API_NoteAutotextID::CoreTopElevToFirstRL,
			API_NoteAutotextID::CoreTopElevToSecondRL,
			API_NoteAutotextID::CoreTopElevToAltitude,
			API_NoteAutotextID::CoreBottomElevToPZ,
			API_NoteAutotextID::CoreBottomElevToFirstRL,
			API_NoteAutotextID::CoreBottomElevToSecondRL,
			API_NoteAutotextID::CoreBottomElevToAltitude,
			API_NoteAutotextID::BottomElevToPZ,
			API_NoteAutotextID::BottomElevToFirstRL,
			API_NoteAutotextID::BottomElevToSecondRL,
			API_NoteAutotextID::BottomElevToAltitude,
			API_NoteAutotextID::ProjectZeroName,
			API_NoteAutotextID::FirstReferenceLevelName,
			API_NoteAutotextID::SecondReferenceLevelName,
			API_NoteAutotextID::AltitudeName,
			API_NoteAutotextID::StoredOriginName
		};
		for (API_NoteAutotextID id : kAllNoteAutotexts) {
			GS::UniString kw;
			if (ACAPI_AutoText_GetNoteKeywordForAutotextID (id, kw) == NoError && !kw.IsEmpty ()) {
				kw.SetToLowerCase ();
				sKeywordsLower.Push (kw);
			}
		}
		/* Pojistka pro výchozí klíčové slovo <MeasuredValue> */
		sKeywordsLower.Push (GS::UniString ("<measuredvalue>"));
	}
	return sKeywordsLower;
}

static bool ContainsAnyAutotextTag (const GS::UniString& text)
{
	if (text.IsEmpty ()) {
		return false;
	}
	GS::UniString lower = text;
	lower.SetToLowerCase ();

	const GS::Array<GS::UniString>& noteKeywords = GetDimensionAutotextKeywordsLower ();
	for (const GS::UniString& kw : noteKeywords) {
		if (lower.Contains (kw)) {
			return true;
		}
	}

	/* Obecná detekce libovolného autotextového tagu Archicadu ve tvaru <Identifikator> */
	const auto utf8 = text.ToCStr (0, MaxUSize, CC_UTF8);
	const char* p = utf8.Get ();
	if (p == nullptr) {
		return false;
	}
	for (size_t i = 0; p[i] != '\0'; ++i) {
		if (p[i] == '<') {
			const char first = p[i + 1];
			if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z'))) {
				continue;
			}
			size_t j = i + 2;
			while (p[j] != '\0' && p[j] != '>' && p[j] != '<') {
				const char c = p[j];
				const bool validChar =
					(c >= 'A' && c <= 'Z') ||
					(c >= 'a' && c <= 'z') ||
					(c >= '0' && c <= '9') ||
					c == '_' || c == '-';
				if (!validChar) {
					break;
				}
				++j;
			}
			if (p[j] == '>' && (j - (i + 1)) >= 3) {
				return true;
			}
		}
	}
	return false;
}

static bool IsNoteMissingMeasuredValue (const API_NoteType& note, GS::UniString& outCustomText)
{
	if (note.contentType != API_NoteContent_Custom) {
		return false;
	}
	GS::UniString customStr;
	if (note.contentUStr != nullptr && !note.contentUStr->IsEmpty ()) {
		customStr = *note.contentUStr;
	} else if (note.content[0] != '\0') {
		customStr = GS::UniString (note.content, CC_UTF8);
		if (customStr.IsEmpty ()) {
			customStr = GS::UniString (note.content, CC_Default);
		}
	}
	if (ContainsAnyAutotextTag (customStr)) {
		return false;
	}
	outCustomText = customStr.IsEmpty ()
		? GS::UniString (u"(prázdný vlastní text)")
		: customStr;
	return true;
}

static void CheckCustomTextDimensions (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::HashTable<short, GS::UniString> storyMap;
	BuildStoryNameMap (storyMap);
	GS::HashTable<Int32, GS::UniString> layerCache;

	const API_ElemTypeID dimTypes[] = {
		API_DimensionID,
		API_RadialDimensionID,
		API_LevelDimensionID,
		API_AngleDimensionID
	};

	for (API_ElemTypeID dimTypeID : dimTypes) {
		GS::Array<API_Guid> dimGuids;
		if (ACAPI_Element_GetElemList (dimTypeID, &dimGuids) != NoError) {
			continue;
		}
		for (const API_Guid& guid : dimGuids) {
			API_Element elem;
			BNZeroMemory (&elem, sizeof (API_Element));
			elem.header.guid = guid;

			GS::UniString uNote1;
			GS::UniString uNote2;
			if (dimTypeID == API_RadialDimensionID) {
				elem.radialDimension.note.contentUStr = &uNote1;
			} else if (dimTypeID == API_LevelDimensionID) {
				elem.levelDimension.note1.contentUStr = &uNote1;
				elem.levelDimension.note2.contentUStr = &uNote2;
			} else if (dimTypeID == API_AngleDimensionID) {
				elem.angleDimension.note.contentUStr = &uNote1;
			}

			if (ACAPI_Element_Get (&elem) != NoError) {
				continue;
			}

			const GS::UniString typeName  = GetElemTypeName (elem.header.type);
			const GS::UniString storyName = LookupStoryName (elem.header.floorInd, storyMap);
			const GS::UniString layerName = LookupLayerName (elem, kElemCap_Layer, layerCache);

			auto pushDimRow = [&] (const GS::UniString& customText, Int32 subElemIdx) {
				AuditRow row;
				row.elemGuid     = guid;
				row.is3DCapable  = false;
				row.subElemIndex = subElemIdx;
				row.cells.Push (typeName);
				row.cells.Push (storyName);
				row.cells.Push (layerName);
				row.cells.Push (customText);
				inOutState.rows.Push (row);
			};

			if (dimTypeID == API_DimensionID) {
				API_ElementMemo memo;
				BNZeroMemory (&memo, sizeof (API_ElementMemo));
				if (ACAPI_Element_GetMemo (guid, &memo, APIMemoMask_All) == NoError && memo.dimElems != nullptr) {
					const Int32 nDim = BMGetHandleSize (reinterpret_cast<GSHandle> (memo.dimElems)) /
									   static_cast<Int32> (sizeof (API_DimElem));
					const Int32 startIdx = (elem.dimension.dimAppear == APIApp_Elev || nDim == 1) ? 0 : 1;
					const Int32 segmentCount = nDim - startIdx;
					for (Int32 i = startIdx; i < nDim; ++i) {
						GS::UniString customText;
						if (IsNoteMissingMeasuredValue ((*memo.dimElems)[i].note, customText)) {
							const Int32 displayNum = (startIdx == 0) ? (i + 1) : i;
							if (segmentCount > 1) {
								pushDimRow (GS::UniString (u"Úsek ") + GS::ValueToUniString (static_cast<int> (displayNum)) + GS::UniString (u": ") + customText, i);
							} else {
								pushDimRow (customText, i);
							}
						}
					}
				}
				ACAPI_DisposeElemMemoHdls (&memo);
			} else if (dimTypeID == API_RadialDimensionID) {
				GS::UniString customText;
				if (IsNoteMissingMeasuredValue (elem.radialDimension.note, customText)) {
					pushDimRow (customText, 0);
				}
			} else if (dimTypeID == API_LevelDimensionID) {
				GS::UniString customText1;
				if (IsNoteMissingMeasuredValue (elem.levelDimension.note1, customText1)) {
					pushDimRow (customText1, 1);
				}
				if (elem.levelDimension.dimForm == 8 || elem.levelDimension.dimForm == 9) {
					GS::UniString customText2;
					if (IsNoteMissingMeasuredValue (elem.levelDimension.note2, customText2)) {
						pushDimRow (customText2, 2);
					}
				}
			} else if (dimTypeID == API_AngleDimensionID) {
				GS::UniString customText;
				if (IsNoteMissingMeasuredValue (elem.angleDimension.note, customText)) {
					pushDimRow (customText, 0);
				}
			}
		}
	}

	ACAPI_WriteReport ("Kontrola modelu (Kóty bez autotextu): nalezeno %d přepsaných textů kót.",
		false, static_cast<int> (inOutState.rows.GetSize ()));
}

static bool IsDimBaseStatic (const API_DimBase& dimBase)
{
	if (dimBase.base.guid == APINULLGuid || dimBase.base.type.typeID == API_ZombieElemID) {
		return true;
	}
	API_Elem_Head refHead;
	BNZeroMemory (&refHead, sizeof (API_Elem_Head));
	refHead.guid = dimBase.base.guid;
	return (ACAPI_Element_GetHeader (&refHead) != NoError);
}

static API_Coord ComputeAngleArmMarkerPos (
	const API_AngleDimensionType&	angleDim,
	const API_DimBase&				b0,
	const API_DimBase&				b1)
{
	const API_Coord origo = angleDim.origo;
	double radius = angleDim.radius;
	if (radius <= 1e-6) {
		radius = std::hypot (angleDim.pos.x - origo.x, angleDim.pos.y - origo.y);
	}
	const double d0 = std::hypot (b0.loc.x - origo.x, b0.loc.y - origo.y);
	const double d1 = std::hypot (b1.loc.x - origo.x, b1.loc.y - origo.y);
	const API_Coord farPt = (d1 > d0) ? b1.loc : b0.loc;
	const double dist = (d1 > d0) ? d1 : d0;
	if (dist > 1e-6 && radius > 1e-6) {
		return {
			origo.x + radius * ((farPt.x - origo.x) / dist),
			origo.y + radius * ((farPt.y - origo.y) / dist)
		};
	}
	return farPt;
}

static void CheckStaticDimensions (SubCheckState& inOutState)
{
	inOutState.rows.Clear ();
	inOutState.hasRun = true;

	GS::HashTable<short, GS::UniString> storyMap;
	BuildStoryNameMap (storyMap);
	GS::HashTable<Int32, GS::UniString> layerCache;

	const API_ElemTypeID dimTypes[] = {
		API_DimensionID,
		API_RadialDimensionID,
		API_LevelDimensionID,
		API_AngleDimensionID
	};

	for (API_ElemTypeID dimTypeID : dimTypes) {
		GS::Array<API_Guid> dimGuids;
		if (ACAPI_Element_GetElemList (dimTypeID, &dimGuids) != NoError) {
			continue;
		}
		for (const API_Guid& guid : dimGuids) {
			API_Element elem;
			BNZeroMemory (&elem, sizeof (API_Element));
			elem.header.guid = guid;
			if (ACAPI_Element_Get (&elem) != NoError) {
				continue;
			}

			const GS::UniString typeName  = GetElemTypeName (elem.header.type);
			const GS::UniString storyName = LookupStoryName (elem.header.floorInd, storyMap);
			const GS::UniString layerName = LookupLayerName (elem, kElemCap_Layer, layerCache);

			auto pushStaticRow = [&] (const GS::UniString& staticPartDesc, Int32 subElemIdx, const API_Coord& markerPos) {
				AuditRow row;
				row.elemGuid     = guid;
				row.is3DCapable  = false;
				row.subElemIndex = subElemIdx;
				row.hasMarkerPos = true;
				row.markerPos    = markerPos;
				row.cells.Push (typeName);
				row.cells.Push (storyName);
				row.cells.Push (layerName);
				row.cells.Push (staticPartDesc);
				inOutState.rows.Push (row);
			};

			if (dimTypeID == API_DimensionID) {
				API_ElementMemo memo;
				BNZeroMemory (&memo, sizeof (API_ElementMemo));
				if (ACAPI_Element_GetMemo (guid, &memo, APIMemoMask_All) == NoError && memo.dimElems != nullptr) {
					const Int32 nDim = BMGetHandleSize (reinterpret_cast<GSHandle> (memo.dimElems)) /
									   static_cast<Int32> (sizeof (API_DimElem));
					for (Int32 i = 0; i < nDim; ++i) {
						const API_DimElem& dimElem = (*memo.dimElems)[i];
						if (elem.dimension.defStaticDim || IsDimBaseStatic (dimElem.base)) {
							const GS::UniString desc = GS::UniString (u"Hraničící čára č. ") +
								GS::ValueToUniString (static_cast<int> (i + 1));
							pushStaticRow (desc, i, dimElem.pos);
						}
					}
				}
				ACAPI_DisposeElemMemoHdls (&memo);
			} else if (dimTypeID == API_RadialDimensionID) {
				if (IsDimBaseStatic (elem.radialDimension.base)) {
					pushStaticRow (
						GS::UniString (u"Statická radiální kóta"),
						0,
						elem.radialDimension.base.loc);
				}
			} else if (dimTypeID == API_LevelDimensionID) {
				if (elem.levelDimension.staticLevel) {
					pushStaticRow (
						GS::UniString (u"Statická výšková kóta"),
						0,
						elem.levelDimension.loc);
				}
			} else if (dimTypeID == API_AngleDimensionID) {
				auto isAngleArmStatic = [] (const API_DimBase& b0, const API_DimBase& b1) -> bool {
					if (b0.base.line || b1.base.line) {
						return IsDimBaseStatic (b0) && IsDimBaseStatic (b1);
					}
					return IsDimBaseStatic (b0) || IsDimBaseStatic (b1);
				};
				if (isAngleArmStatic (elem.angleDimension.base[0], elem.angleDimension.base[1])) {
					pushStaticRow (
						GS::UniString (u"Hraničící čára (rameno č. 1)"),
						0,
						ComputeAngleArmMarkerPos (elem.angleDimension, elem.angleDimension.base[0], elem.angleDimension.base[1]));
				}
				if (isAngleArmStatic (elem.angleDimension.base[2], elem.angleDimension.base[3])) {
					pushStaticRow (
						GS::UniString (u"Hraničící čára (rameno č. 2)"),
						1,
						ComputeAngleArmMarkerPos (elem.angleDimension, elem.angleDimension.base[2], elem.angleDimension.base[3]));
				}
			}
		}
	}

	ACAPI_WriteReport ("Kontrola modelu (Statické části kót): nalezeno %d statických částí kót.",
		false, static_cast<int> (inOutState.rows.GetSize ()));
}

} // namespace

/* ============================================================================
 * PUBLIC API — 3: PRVKY MODELU
 * ============================================================================ */

void InitElementSubChecks (GS::Array<SubCheckState>& outSubChecks)
{
	outSubChecks.Clear ();

	/* 0: Chybějící atributy ve vložených prvcích */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Chybějící atributy ve vložených prvcích");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Typ prvku"), 120 });
		st.columns.Push ({ GS::UniString (u"ID prvku"), 110 });
		st.columns.Push ({ GS::UniString (u"Domovské podlaží"), 130 });
		st.columns.Push ({ GS::UniString (u"Typ chybějícího atributu"), 180 });
		st.columns.Push ({ GS::UniString (u"Parametr prvku"), 180 });
		outSubChecks.Push (st);
	}

	/* 1: Nezaklasifikované prvky */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Nezaklasifikované prvky");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Typ prvku"), 120 });
		st.columns.Push ({ GS::UniString (u"ID prvku"), 110 });
		st.columns.Push ({ GS::UniString (u"Domovské podlaží"), 130 });
		st.columns.Push ({ GS::UniString (u"Vrstva"), 150 });
		st.columns.Push ({ GS::UniString (u"Chybějící klasifikace"), 210 });
		outSubChecks.Push (st);
	}

	/* 2: Prvky bez definované konstrukční funkce */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Prvky bez definované konstrukční funkce");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Typ prvku"), 180 });
		st.columns.Push ({ GS::UniString (u"ID prvku"), 150 });
		st.columns.Push ({ GS::UniString (u"Domovské podlaží"), 180 });
		st.columns.Push ({ GS::UniString (u"Vrstva"), 210 });
		outSubChecks.Push (st);
	}

	/* 3: Prvky bez definovaného umístění */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Prvky bez definovaného umístění");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Typ prvku"), 180 });
		st.columns.Push ({ GS::UniString (u"ID prvku"), 150 });
		st.columns.Push ({ GS::UniString (u"Domovské podlaží"), 180 });
		st.columns.Push ({ GS::UniString (u"Vrstva"), 210 });
		outSubChecks.Push (st);
	}

	/* 4: Prvky ve vrstvě Archicad */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Prvky ve vrstvě Archicad");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Typ prvku"), 240 });
		st.columns.Push ({ GS::UniString (u"ID prvku"), 220 });
		st.columns.Push ({ GS::UniString (u"Domovské podlaží"), 260 });
		outSubChecks.Push (st);
	}
}

void RunElementSubCheck (
	ElementSubCheckId				subCheckId,
	const GS::HashSet<API_Guid>&	disabledClassificationSystems,
	SubCheckState&					inOutState)
{
	switch (subCheckId) {
		case ElementSubCheckId::MissingAttributesInElements:
			CheckMissingAttributesInElements (inOutState);
			break;
		case ElementSubCheckId::UnclassifiedElements:
			CheckUnclassifiedElements (disabledClassificationSystems, inOutState);
			break;
		case ElementSubCheckId::UndefinedStructuralFunction:
			CheckUndefinedCategoryValue (
				API_ElemCategory_StructuralFunction,
				kElemCap_StructuralFunction,
				"CCCC2E1F-54E6-8534-EEA0-F571386498E1",
				inOutState);
			break;
		case ElementSubCheckId::UndefinedPosition:
			CheckUndefinedCategoryValue (
				API_ElemCategory_Position,
				kElemCap_Position,
				"677677E5-E514-E73F-39B9-E7AFC2031846",
				inOutState);
			break;
		case ElementSubCheckId::ElementsOnArchicadLayer:
			CheckElementsOnArchicadLayer (inOutState);
			break;
		default:
			break;
	}
}

/* ============================================================================
 * PUBLIC API — 4: VÝCHOZÍ NASTAVENÍ NÁSTROJŮ
 * ============================================================================ */

void InitDefaultSubChecks (GS::Array<SubCheckState>& outSubChecks)
{
	outSubChecks.Clear ();

	/* 0: Chybějící atributy ve výchozím nastavení */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Chybějící atributy ve výchozím nastavení");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Nástroj"), 180 });
		st.columns.Push ({ GS::UniString (u"Typ chybějícího atributu"), 240 });
		st.columns.Push ({ GS::UniString (u"Parametr nástroje"), 300 });
		outSubChecks.Push (st);
	}

	/* 1: Nezaklasifikované výchozí nastavení */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Nezaklasifikované výchozí nastavení");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Nástroj"), 180 });
		st.columns.Push ({ GS::UniString (u"Vrstva"), 220 });
		st.columns.Push ({ GS::UniString (u"Chybějící klasifikace"), 320 });
		outSubChecks.Push (st);
	}

	/* 2: Výchozí nastavení bez konstrukční funkce */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Výchozí nastavení bez konstrukční funkce");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Nástroj"), 340 });
		st.columns.Push ({ GS::UniString (u"Vrstva"), 380 });
		outSubChecks.Push (st);
	}

	/* 3: Výchozí nastavení bez umístění */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Výchozí nastavení bez umístění");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Nástroj"), 340 });
		st.columns.Push ({ GS::UniString (u"Vrstva"), 380 });
		outSubChecks.Push (st);
	}

	/* 4: Výchozí nastavení ve vrstvě Archicad */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Výchozí nastavení ve vrstvě Archicad");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Nástroj"), 720 });
		outSubChecks.Push (st);
	}
}

void RunDefaultSubCheck (
	ElementSubCheckId				subCheckId,
	const GS::HashSet<API_Guid>&	disabledClassificationSystems,
	SubCheckState&					inOutState)
{
	switch (subCheckId) {
		case ElementSubCheckId::MissingAttributesInElements:
			CheckMissingAttributesInDefaults (inOutState);
			break;
		case ElementSubCheckId::UnclassifiedElements:
			CheckUnclassifiedDefaults (disabledClassificationSystems, inOutState);
			break;
		case ElementSubCheckId::UndefinedStructuralFunction:
			CheckUndefinedCategoryDefault (
				API_ElemCategory_StructuralFunction,
				kElemCap_StructuralFunction,
				"CCCC2E1F-54E6-8534-EEA0-F571386498E1",
				inOutState);
			break;
		case ElementSubCheckId::UndefinedPosition:
			CheckUndefinedCategoryDefault (
				API_ElemCategory_Position,
				kElemCap_Position,
				"677677E5-E514-E73F-39B9-E7AFC2031846",
				inOutState);
			break;
		case ElementSubCheckId::ElementsOnArchicadLayer:
			CheckDefaultsOnArchicadLayer (inOutState);
			break;
		default:
			break;
	}
}

/* ============================================================================
 * PUBLIC API — 5: OBLÍBENÁ NASTAVENÍ
 * ============================================================================ */

void InitFavoriteSubChecks (GS::Array<SubCheckState>& outSubChecks)
{
	outSubChecks.Clear ();

	/* 0: Chybějící atributy v oblíbených */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Chybějící atributy v oblíbených");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Název oblíbeného"), 190 });
		st.columns.Push ({ GS::UniString (u"Nástroj"), 115 });
		st.columns.Push ({ GS::UniString (u"Složka"), 130 });
		st.columns.Push ({ GS::UniString (u"Typ chybějícího atributu"), 150 });
		st.columns.Push ({ GS::UniString (u"Parametr"), 135 });
		outSubChecks.Push (st);
	}

	/* 1: Nezaklasifikované oblíbené */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Nezaklasifikované oblíbené");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Název oblíbeného"), 190 });
		st.columns.Push ({ GS::UniString (u"Nástroj"), 115 });
		st.columns.Push ({ GS::UniString (u"Složka"), 130 });
		st.columns.Push ({ GS::UniString (u"Vrstva"), 125 });
		st.columns.Push ({ GS::UniString (u"Chybějící klasifikace"), 160 });
		outSubChecks.Push (st);
	}

	/* 2: Oblíbené bez konstrukční funkce */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Oblíbené bez konstrukční funkce");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Název oblíbeného"), 250 });
		st.columns.Push ({ GS::UniString (u"Nástroj"), 150 });
		st.columns.Push ({ GS::UniString (u"Složka"), 160 });
		st.columns.Push ({ GS::UniString (u"Vrstva"), 160 });
		outSubChecks.Push (st);
	}

	/* 3: Oblíbené bez umístění */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Oblíbené bez umístění");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Název oblíbeného"), 250 });
		st.columns.Push ({ GS::UniString (u"Nástroj"), 150 });
		st.columns.Push ({ GS::UniString (u"Složka"), 160 });
		st.columns.Push ({ GS::UniString (u"Vrstva"), 160 });
		outSubChecks.Push (st);
	}

	/* 4: Oblíbené ve vrstvě Archicad */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Oblíbené ve vrstvě Archicad");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Název oblíbeného"), 300 });
		st.columns.Push ({ GS::UniString (u"Nástroj"), 200 });
		st.columns.Push ({ GS::UniString (u"Složka"), 220 });
		outSubChecks.Push (st);
	}
}

void RunFavoriteSubCheck (
	ElementSubCheckId				subCheckId,
	const GS::HashSet<API_Guid>&	disabledClassificationSystems,
	SubCheckState&					inOutState)
{
	switch (subCheckId) {
		case ElementSubCheckId::MissingAttributesInElements:
			CheckMissingAttributesInFavorites (inOutState);
			break;
		case ElementSubCheckId::UnclassifiedElements:
			CheckUnclassifiedFavorites (disabledClassificationSystems, inOutState);
			break;
		case ElementSubCheckId::UndefinedStructuralFunction:
			CheckUndefinedCategoryFavorite (
				API_ElemCategory_StructuralFunction,
				kElemCap_StructuralFunction,
				"CCCC2E1F-54E6-8534-EEA0-F571386498E1",
				inOutState);
			break;
		case ElementSubCheckId::UndefinedPosition:
			CheckUndefinedCategoryFavorite (
				API_ElemCategory_Position,
				kElemCap_Position,
				"677677E5-E514-E73F-39B9-E7AFC2031846",
				inOutState);
			break;
		case ElementSubCheckId::ElementsOnArchicadLayer:
			CheckFavoritesOnArchicadLayer (inOutState);
			break;
		default:
			break;
	}
}

/* ============================================================================
 * PUBLIC API — 2: VLASTNOSTI
 * ============================================================================ */

void InitPropertySubChecks (GS::Array<SubCheckState>& outSubChecks)
{
	outSubChecks.Clear ();

	/* 0: Nedostupné pro žádný klas. systém */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Nedostupné pro žádný klas. systém");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Skupina vlastností"), 240 });
		st.columns.Push ({ GS::UniString (u"Název vlastnosti"), 280 });
		st.columns.Push ({ GS::UniString (u"Datový typ"), 200 });
		outSubChecks.Push (st);
	}
}

void RunPropertySubCheck (
	PropertySubCheckId	subCheckId,
	SubCheckState&		inOutState)
{
	switch (subCheckId) {
		case PropertySubCheckId::UnassignedProperties:
			CheckUnassignedProperties (inOutState);
			break;
		default:
			break;
	}
}

/* ============================================================================
 * PUBLIC API — 6: ZOBRAZENÍ
 * ============================================================================ */

void InitViewSubChecks (GS::Array<SubCheckState>& outSubChecks)
{
	outSubChecks.Clear ();

	/* 0: Nepoužité sady per */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Nepoužité sady per");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Název sady per"), 560 });
		st.columns.Push ({ GS::UniString (u"Index"), 180 });
		outSubChecks.Push (st);
	}

	/* 1: Nepoužité kombinace vrstev */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Nepoužité kombinace vrstev");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Název kombinace vrstev"), 560 });
		st.columns.Push ({ GS::UniString (u"Index"), 180 });
		outSubChecks.Push (st);
	}

	/* 2: Nepoužité filtry rekonstrukcí */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Nepoužité filtry rekonstrukcí");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Název filtru rekonstrukce"), 740 });
		outSubChecks.Push (st);
	}

	/* 3: Nepoužité 3D styly */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Nepoužité 3D styly");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Název 3D stylu"), 740 });
		outSubChecks.Push (st);
	}

	/* 4: Nepoužité kombinace grafických stylů */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Nepoužité kombinace grafických stylů");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Název kombinace grafických stylů"), 740 });
		outSubChecks.Push (st);
	}

	/* 5: Nepoužité volby zobrazení modelu (VZM) */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Nepoužité volby zobrazení modelu (VZM)");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Název voleb zobrazení modelu (VZM)"), 560 });
		st.columns.Push ({ GS::UniString (u"Index"), 180 });
		outSubChecks.Push (st);
	}
}

void RunViewSubCheck (
	ViewSubCheckId	subCheckId,
	SubCheckState&	inOutState)
{
	switch (subCheckId) {
		case ViewSubCheckId::UnusedPenTables:
			CheckUnusedPenTables (inOutState);
			break;
		case ViewSubCheckId::UnusedLayerCombinations:
			CheckUnusedLayerCombinations (inOutState);
			break;
		case ViewSubCheckId::UnusedRenovationFilters:
			CheckUnusedRenovationFilters (inOutState);
			break;
		case ViewSubCheckId::Unused3DStyles:
			CheckUnused3DStyles (inOutState);
			break;
		case ViewSubCheckId::UnusedGraphicOverrides:
			CheckUnusedGraphicOverrides (inOutState);
			break;
		case ViewSubCheckId::UnusedModelViewOptions:
			CheckUnusedModelViewOptions (inOutState);
			break;
		default:
			break;
	}
}

/* ============================================================================
 * PUBLIC API — 7: DOKUMENTACE
 * ============================================================================ */

void InitDocSubChecks (GS::Array<SubCheckState>& outSubChecks)
{
	outSubChecks.Clear ();

	/* 0: Neasociované popisky */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Neasociované popisky");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Typ popisky"), 260 });
		st.columns.Push ({ GS::UniString (u"Domovské podlaží"), 220 });
		st.columns.Push ({ GS::UniString (u"Vrstva"), 240 });
		outSubChecks.Push (st);
	}

	/* 1: Kóty bez naměřené hodnoty (autotextu) */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Kóty bez naměřené hodnoty (autotextu)");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Typ kóty"), 170 });
		st.columns.Push ({ GS::UniString (u"Domovské podlaží"), 160 });
		st.columns.Push ({ GS::UniString (u"Vrstva"), 180 });
		st.columns.Push ({ GS::UniString (u"Vlastní text kóty"), 210 });
		outSubChecks.Push (st);
	}

	/* 2: Statické části kót */
	{
		SubCheckState st;
		st.baseName = GS::UniString (u"Statické části kót");
		st.checked  = true;
		st.columns.Push ({ GS::UniString (u"Typ kóty"), 170 });
		st.columns.Push ({ GS::UniString (u"Domovské podlaží"), 160 });
		st.columns.Push ({ GS::UniString (u"Vrstva"), 180 });
		st.columns.Push ({ GS::UniString (u"Statická část kóty"), 210 });
		outSubChecks.Push (st);
	}
}

void RunDocSubCheck (
	DocSubCheckId	subCheckId,
	SubCheckState&	inOutState)
{
	switch (subCheckId) {
		case DocSubCheckId::UnassociatedLabels:
			CheckUnassociatedLabels (inOutState);
			break;
		case DocSubCheckId::CustomTextDimensions:
			CheckCustomTextDimensions (inOutState);
			break;
		case DocSubCheckId::StaticDimensions:
			CheckStaticDimensions (inOutState);
			break;
		default:
			break;
	}
}
