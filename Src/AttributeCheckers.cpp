#include "AttributeCheckers.hpp"
#include "APIdefs_Attributes.h"
#include "API_AttributeIndexUtils.hpp"
#include "ProfileVectorImage.hpp"
#include "VectorImageIterator.hpp"

#include <cmath>
#include <cstring>

namespace {

constexpr double kAttrEps = 1e-5;

static bool NearlyEqual (double a, double b, double eps = kAttrEps)
{
	return std::fabs (a - b) <= eps;
}

static bool NearlyEqual (const API_Coord& a, const API_Coord& b, double eps = kAttrEps)
{
	return NearlyEqual (a.x, b.x, eps) && NearlyEqual (a.y, b.y, eps);
}

static bool NearlyEqualRGB (const API_RGBColor& a, const API_RGBColor& b)
{
	return NearlyEqual (a.f_red, b.f_red, 1e-3) &&
		   NearlyEqual (a.f_green, b.f_green, 1e-3) &&
		   NearlyEqual (a.f_blue, b.f_blue, 1e-3);
}

static void CleanupAttributeMemory (API_Attribute& attr)
{
	if (attr.header.typeID == API_MaterialID && attr.material.texture.fileLoc != nullptr) {
		delete attr.material.texture.fileLoc;
		attr.material.texture.fileLoc = nullptr;
	}
}

struct SafeLoadedAttr {
	API_Attribute	attr;
	GS::UniString	name;

	SafeLoadedAttr ()
	{
		BNZeroMemory (&attr, sizeof (API_Attribute));
	}
};

static void LoadAttributesOfType (API_AttrTypeID typeID, GS::Array<SafeLoadedAttr>& outList)
{
	outList.Clear ();
	UInt32 count = 0;
	if (ACAPI_Attribute_GetNum (typeID, count) != NoError || count == 0) {
		return;
	}
	for (UInt32 i = 1; i <= count; ++i) {
		SafeLoadedAttr item;
		item.attr.header.typeID = typeID;
		item.attr.header.index  = ACAPI_CreateAttributeIndex (static_cast<Int32> (i));
		item.attr.header.uniStringNamePtr = &item.name;
		if (ACAPI_Attribute_Get (&item.attr) != NoError) {
			CleanupAttributeMemory (item.attr);
			continue;
		}
		CleanupAttributeMemory (item.attr);
		item.attr.header.uniStringNamePtr = nullptr;
		if (item.name.IsEmpty ()) {
			item.name = GS::UniString (item.attr.header.name, CC_Default);
		}
		outList.Push (item);
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
		case API_LayerCombID:			return GS::UniString (u"Kombinace vrstev");
		case API_ZoneCatID:				return GS::UniString (u"Kategorie zón");
		case API_ProfileID:				return GS::UniString (u"Vlastní profil");
		case API_PenTableID:			return GS::UniString (u"Sada per");
		case API_MEPSystemID:			return GS::UniString (u"Tvarovka TZB");
		case API_OperationProfileID:	return GS::UniString (u"Profil provozu");
		case API_BuildingMaterialID:	return GS::UniString (u"Stavební materiál");
		default:						return GS::UniString (u"Atribut");
	}
}

static bool IsValidAttribute (API_AttrTypeID typeID, const API_AttributeIndex& index)
{
	/* U povrchového materiálu znamená index 0 platný stav "Dle stavebního materiálu / kategorie" */
	if (typeID == API_MaterialID && index.ToInt32_Deprecated () == 0) {
		return true;
	}
	if (index == APIInvalidAttributeIndex || index.ToInt32_Deprecated () <= 0) {
		return false;
	}
	API_Attribute testAttr;
	BNZeroMemory (&testAttr, sizeof (API_Attribute));
	testAttr.header.typeID = typeID;
	testAttr.header.index  = index;
	const GSErrCode err = ACAPI_Attribute_Get (&testAttr);
	CleanupAttributeMemory (testAttr);
	return (err == NoError);
}

struct MissingAttrKey {
	API_AttrTypeID	missingType = API_ZombieAttrID;
	Int32			missingIdx	= 0;

	bool operator== (const MissingAttrKey& o) const
	{
		return missingType == o.missingType && missingIdx == o.missingIdx;
	}
};

static void AddMissingIfInvalid (
	API_AttrTypeID					parentType,
	const API_AttributeIndex&		parentIdx,
	const GS::UniString&			parentName,
	API_AttrTypeID					refType,
	const API_AttributeIndex&		refIdx,
	std::u16string_view				roleName,
	GS::Array<MissingAttrKey>&		reportedForParent,
	GS::Array<AuditRow>&			outRows)
{
	if (IsValidAttribute (refType, refIdx)) {
		return;
	}
	const Int32 rawIdx = refIdx.ToInt32_Deprecated ();
	MissingAttrKey key { refType, rawIdx };
	for (const MissingAttrKey& k : reportedForParent) {
		if (k == key) {
			return;
		}
	}
	reportedForParent.Push (key);

	AuditRow row;
	row.attrType	= parentType;
	row.attrIndex	= parentIdx;
	row.cells.Push (GetAttrTypeNameCZ (parentType));
	row.cells.Push (parentName);
	row.cells.Push (GetAttrTypeNameCZ (refType));
	row.cells.Push (GS::UniString (roleName));
	outRows.Push (row);
}

static void AddMissingIfInvalid (
	API_AttrTypeID					parentType,
	const API_AttributeIndex&		parentIdx,
	const GS::UniString&			parentName,
	API_AttrTypeID					refType,
	const ADB::AttributeIndex&		refIdx,
	std::u16string_view				roleName,
	GS::Array<MissingAttrKey>&		reportedForParent,
	GS::Array<AuditRow>&			outRows)
{
	AddMissingIfInvalid (parentType, parentIdx, parentName, refType, AC2API_AttrIndex (refIdx), roleName, reportedForParent, outRows);
}

static void CheckMissingAttributesInAttributes (GS::Array<AuditRow>& outRows)
{
	outRows.Clear ();

	// 1. Vlastní profily (API_ProfileID)
	{
		GS::Array<SafeLoadedAttr> attrs;
		LoadAttributesOfType (API_ProfileID, attrs);
		for (const SafeLoadedAttr& item : attrs) {
			const API_Attribute& attr = item.attr;
			const GS::UniString& parentName = item.name;
			API_AttributeDefExt defs;
			BNZeroMemory (&defs, sizeof (API_AttributeDefExt));
			if (ACAPI_Attribute_GetDefExt (API_ProfileID, attr.header.index, &defs) == NoError) {
				GS::Array<MissingAttrKey> reported;
				if (defs.profile_vectorImageItems != nullptr) {
					for (ConstProfileVectorImageIterator it (*defs.profile_vectorImageItems); !it.IsEOI (); ++it) {
						switch (it->item_Typ) {
							case SyLine: {
								const Sy_LinType* line = it;
								if (line != nullptr) {
									AddMissingIfInvalid (API_ProfileID, attr.header.index, parentName,
										API_LinetypeID, line->GetPattern (), u"Čára v profilu", reported, outRows);
								}
								break;
							}
							case SyArc: {
								const Sy_ArcType* arc = it;
								if (arc != nullptr) {
									AddMissingIfInvalid (API_ProfileID, attr.header.index, parentName,
										API_LinetypeID, arc->GetPattern (), u"Oblouk v profilu", reported, outRows);
								}
								break;
							}
							case SySpline: {
								const Sy_SplineType* sp = it;
								if (sp != nullptr) {
									AddMissingIfInvalid (API_ProfileID, attr.header.index, parentName,
										API_LinetypeID, sp->GetPattern (), u"Křivka v profilu", reported, outRows);
								}
								break;
							}
							case SyPolyLine: {
								const Sy_PolyLinType* pl = it;
								if (pl != nullptr) {
									AddMissingIfInvalid (API_ProfileID, attr.header.index, parentName,
										API_LinetypeID, pl->GetPattern (), u"Lomená čára v profilu", reported, outRows);
								}
								break;
							}
							case SyHatch: {
								const HatchObject& hatch = it;
								if (hatch.IsBuildingMaterialHatch ()) {
									AddMissingIfInvalid (API_ProfileID, attr.header.index, parentName,
										API_BuildingMaterialID, hatch.GetBuildMatIdx (), u"Stavební materiál průřezu", reported, outRows);
								}
								if (hatch.IsFillHatch ()) {
									AddMissingIfInvalid (API_ProfileID, attr.header.index, parentName,
										API_FilltypeID, hatch.GetFillIdx (), u"Výplň průřezu", reported, outRows);
								}
								if (hatch.IsSurfaceHatch ()) {
									AddMissingIfInvalid (API_ProfileID, attr.header.index, parentName,
										API_MaterialID, hatch.GetSurfaceIdx (), u"Povrchový materiál průřezu", reported, outRows);
								}
								if (hatch.GetContVis ()) {
									AddMissingIfInvalid (API_ProfileID, attr.header.index, parentName,
										API_LinetypeID, hatch.GetContLType (), u"Obrysová čára průřezu", reported, outRows);
								}
								break;
							}
							default:
								break;
						}
					}
				}
				ACAPI_DisposeAttrDefsHdlsExt (&defs);
			}
		}
	}

	// 2. Sendvičové konstrukce (API_CompWallID)
	{
		GS::Array<SafeLoadedAttr> attrs;
		LoadAttributesOfType (API_CompWallID, attrs);
		for (const SafeLoadedAttr& item : attrs) {
			const API_Attribute& attr = item.attr;
			const GS::UniString& parentName = item.name;
			API_AttributeDefExt defs;
			BNZeroMemory (&defs, sizeof (API_AttributeDefExt));
			if (ACAPI_Attribute_GetDefExt (API_CompWallID, attr.header.index, &defs) == NoError) {
				GS::Array<MissingAttrKey> reported;
				if (defs.cwall_compItems != nullptr) {
					for (short i = 0; i < attr.compWall.nComps; ++i) {
						AddMissingIfInvalid (API_CompWallID, attr.header.index, parentName,
							API_BuildingMaterialID, (*defs.cwall_compItems)[i].buildingMaterial, u"Stavební materiál vrstvy", reported, outRows);
					}
				}
				if (defs.cwall_compLItems != nullptr) {
					for (short i = 0; i <= attr.compWall.nComps; ++i) {
						if ((*defs.cwall_compLItems)[i].linePen > 0) {
							AddMissingIfInvalid (API_CompWallID, attr.header.index, parentName,
								API_LinetypeID, (*defs.cwall_compLItems)[i].ltypeInd, u"Dělící čára vrstvy", reported, outRows);
						}
					}
				}
				ACAPI_DisposeAttrDefsHdlsExt (&defs);
			}
		}
	}

	// 3. Stavební materiály (API_BuildingMaterialID)
	{
		GS::Array<SafeLoadedAttr> attrs;
		LoadAttributesOfType (API_BuildingMaterialID, attrs);
		for (const SafeLoadedAttr& item : attrs) {
			const API_Attribute& attr = item.attr;
			const GS::UniString& parentName = item.name;
			GS::Array<MissingAttrKey> reported;
			AddMissingIfInvalid (API_BuildingMaterialID, attr.header.index, parentName,
				API_FilltypeID, attr.buildingMaterial.cutFill, u"Řezová výplň", reported, outRows);
			AddMissingIfInvalid (API_BuildingMaterialID, attr.header.index, parentName,
				API_MaterialID, attr.buildingMaterial.cutMaterial, u"Povrchový materiál", reported, outRows);
		}
	}

	// 4. Povrchové materiály (API_MaterialID)
	{
		GS::Array<SafeLoadedAttr> attrs;
		LoadAttributesOfType (API_MaterialID, attrs);
		for (const SafeLoadedAttr& item : attrs) {
			const API_Attribute& attr = item.attr;
			const GS::UniString& parentName = item.name;
			GS::Array<MissingAttrKey> reported;
			if (attr.material.ifill != APIInvalidAttributeIndex && attr.material.ifill.ToInt32_Deprecated () != 0) {
				AddMissingIfInvalid (API_MaterialID, attr.header.index, parentName,
					API_FilltypeID, attr.material.ifill, u"Povrchová výplň", reported, outRows);
			}
		}
	}
}

static void CheckMissingTexturesInSurfaces (GS::Array<AuditRow>& outRows)
{
	outRows.Clear ();
	GS::Array<SafeLoadedAttr> attrs;
	LoadAttributesOfType (API_MaterialID, attrs);
	for (const SafeLoadedAttr& item : attrs) {
		const GS::UniString texName (item.attr.material.texture.texName);
		if (!texName.IsEmpty () && item.attr.material.texture.missingPict) {
			AuditRow row;
			row.attrType	= API_MaterialID;
			row.attrIndex	= item.attr.header.index;
			row.cells.Push (item.name);
			row.cells.Push (texName);
			outRows.Push (row);
		}
	}
}

static void CheckUnclassifiedAttributes (
	const GS::HashSet<API_Guid>&	disabledClassificationSystems,
	GS::Array<AuditRow>&			outRows)
{
	outRows.Clear ();
	GS::Array<API_ClassificationSystem> systems;
	if (ACAPI_Classification_GetClassificationSystems (systems) != NoError || systems.IsEmpty ()) {
		return;
	}

	GS::Array<SafeLoadedAttr> attrs;
	LoadAttributesOfType (API_BuildingMaterialID, attrs);

	for (const SafeLoadedAttr& item : attrs) {
		GS::Array<GS::Pair<API_Guid, API_Guid>> systemItemPairs;
		(void) ACAPI_Attribute_GetClassificationItems (item.attr.header, systemItemPairs);

		for (const API_ClassificationSystem& sys : systems) {
			if (disabledClassificationSystems.Contains (sys.guid)) {
				continue;
			}
			bool hasClassification = false;
			for (const auto& pair : systemItemPairs) {
				if (pair.first == sys.guid && pair.second != APINULLGuid) {
					hasClassification = true;
					break;
				}
			}
			if (!hasClassification) {
				GS::UniString sysLabel = sys.name;
				if (!sys.editionVersion.IsEmpty ()) {
					sysLabel += GS::UniString (" ") + sys.editionVersion;
				}
				AuditRow row;
				row.attrType	= API_BuildingMaterialID;
				row.attrIndex	= item.attr.header.index;
				row.cells.Push (GetAttrTypeNameCZ (API_BuildingMaterialID));
				row.cells.Push (item.name);
				row.cells.Push (sysLabel);
				outRows.Push (row);
			}
		}
	}
}

static bool AreLinetypesEqual (
	const API_LinetypeType&		a,
	const API_AttributeDefExt&	defA,
	const API_LinetypeType&		b,
	const API_AttributeDefExt&	defB)
{
	if (a.type != b.type || a.nItems != b.nItems) {
		return false;
	}
	if ((a.head.flags & APILine_ScaleWithPlan) != (b.head.flags & APILine_ScaleWithPlan)) {
		return false;
	}
	if (!NearlyEqual (a.defineScale, b.defineScale) || !NearlyEqual (a.period, b.period) || !NearlyEqual (a.height, b.height)) {
		return false;
	}
	if (a.type == APILine_SolidLine) {
		return true;
	}
	if (a.type == APILine_DashedLine) {
		if (defA.ltype_dashItems == nullptr || defB.ltype_dashItems == nullptr) {
			return defA.ltype_dashItems == defB.ltype_dashItems;
		}
		for (short i = 0; i < a.nItems; ++i) {
			if (!NearlyEqual ((*defA.ltype_dashItems)[i].dash, (*defB.ltype_dashItems)[i].dash) ||
				!NearlyEqual ((*defA.ltype_dashItems)[i].gap, (*defB.ltype_dashItems)[i].gap)) {
				return false;
			}
		}
		return true;
	}
	if (a.type == APILine_SymbolLine) {
		if (defA.ltype_lineItems == nullptr || defB.ltype_lineItems == nullptr) {
			return defA.ltype_lineItems == defB.ltype_lineItems;
		}
		for (short i = 0; i < a.nItems; ++i) {
			const API_LineItems& ia = (*defA.ltype_lineItems)[i];
			const API_LineItems& ib = (*defB.ltype_lineItems)[i];
			if (ia.itemType != ib.itemType) {
				return false;
			}
			if (!NearlyEqual (ia.itemBegPos, ib.itemBegPos) ||
				!NearlyEqual (ia.itemEndPos, ib.itemEndPos) ||
				!NearlyEqual (ia.itemRadius, ib.itemRadius) ||
				!NearlyEqual (ia.itemBegAngle, ib.itemBegAngle) ||
				!NearlyEqual (ia.itemEndAngle, ib.itemEndAngle)) {
				return false;
			}
		}
		return true;
	}
	return false;
}

static bool AreFillsEqual (
	const API_FilltypeType&		a,
	const API_AttributeDefExt&	defA,
	const API_FilltypeType&		b,
	const API_AttributeDefExt&	defB)
{
	if (a.subType != b.subType || a.head.flags != b.head.flags || a.isPercentFill != b.isPercentFill) {
		return false;
	}
	if (a.isPercentFill) {
		return NearlyEqual (a.percent, b.percent);
	}
	if (std::memcmp (&a.bitPat, &b.bitPat, sizeof (API_Pattern)) != 0) {
		return false;
	}
	if (a.subType == APIFill_Solid || a.subType == APIFill_Empty ||
		a.subType == APIFill_LinearGradient || a.subType == APIFill_RadialGradient) {
		return true;
	}
	if (!NearlyEqual (a.hXSpac, b.hXSpac) || !NearlyEqual (a.hYSpac, b.hYSpac) || !NearlyEqual (a.hAngle, b.hAngle)) {
		return false;
	}
	if (a.linNumb != b.linNumb || a.hotNumb != b.hotNumb || a.arcNumb != b.arcNumb || a.filNumb != b.filNumb) {
		return false;
	}
	if (a.subType == APIFill_Image) {
		return GS::UniString (a.textureName) == GS::UniString (b.textureName) &&
			   a.textureStatus == b.textureStatus &&
			   NearlyEqual (a.textureXSize, b.textureXSize) &&
			   NearlyEqual (a.textureYSize, b.textureYSize) &&
			   NearlyEqual (a.textureRotAng, b.textureRotAng);
	}
	if (a.linNumb > 0 && defA.fill_lineItems != nullptr && defB.fill_lineItems != nullptr) {
		for (Int32 i = 0; i < a.linNumb; ++i) {
			const API_FillLine& la = (*defA.fill_lineItems)[i];
			const API_FillLine& lb = (*defB.fill_lineItems)[i];
			if (!NearlyEqual (la.lFreq, lb.lFreq) || !NearlyEqual (la.lDir, lb.lDir) ||
				!NearlyEqual (la.lOffsetLine, lb.lOffsetLine) ||
				!NearlyEqual (la.lOffset.x, lb.lOffset.x) || !NearlyEqual (la.lOffset.y, lb.lOffset.y) ||
				la.lPartNumb != lb.lPartNumb) {
				return false;
			}
		}
	}
	return true;
}

static bool AreMaterialsEqual (const API_MaterialType& a, const API_MaterialType& b)
{
	if (a.mtype != b.mtype ||
		a.ambientPc != b.ambientPc ||
		a.diffusePc != b.diffusePc ||
		a.specularPc != b.specularPc ||
		a.transpPc != b.transpPc ||
		a.shine != b.shine ||
		a.transpAtt != b.transpAtt ||
		a.emissionAtt != b.emissionAtt ||
		a.fillCol != b.fillCol ||
		a.ifill != b.ifill) {
		return false;
	}
	if (!NearlyEqualRGB (a.surfaceRGB, b.surfaceRGB) ||
		!NearlyEqualRGB (a.specularRGB, b.specularRGB) ||
		!NearlyEqualRGB (a.emissionRGB, b.emissionRGB)) {
		return false;
	}
	const GS::UniString texA (a.texture.texName);
	const GS::UniString texB (b.texture.texName);
	if (texA != texB) {
		return false;
	}
	if (!texA.IsEmpty ()) {
		if (a.texture.status != b.texture.status ||
			!NearlyEqual (a.texture.xSize, b.texture.xSize) ||
			!NearlyEqual (a.texture.ySize, b.texture.ySize) ||
			!NearlyEqual (a.texture.rotAng, b.texture.rotAng)) {
			return false;
		}
	}
	return true;
}

static bool AreBuildingMaterialsEqual (const API_BuildingMaterialType& a, const API_BuildingMaterialType& b)
{
	return a.connPriority == b.connPriority &&
		   a.cutFill == b.cutFill &&
		   a.cutFillPen == b.cutFillPen &&
		   a.cutFillBackgroundPen == b.cutFillBackgroundPen &&
		   a.cutMaterial == b.cutMaterial &&
		   a.cutFillOrientation == b.cutFillOrientation &&
		   NearlyEqual (a.thermalConductivity, b.thermalConductivity) &&
		   NearlyEqual (a.density, b.density) &&
		   NearlyEqual (a.heatCapacity, b.heatCapacity) &&
		   NearlyEqual (a.embodiedEnergy, b.embodiedEnergy) &&
		   NearlyEqual (a.embodiedCarbon, b.embodiedCarbon) &&
		   a.showUncutLines == b.showUncutLines &&
		   a.doNotParticipateInCollDet == b.doNotParticipateInCollDet;
}

static bool AreCompWallsEqual (
	const API_CompWallType&		a,
	const API_AttributeDefExt&	defA,
	const API_CompWallType&		b,
	const API_AttributeDefExt&	defB)
{
	if (a.head.flags != b.head.flags || a.nComps != b.nComps || !NearlyEqual (a.totalThick, b.totalThick)) {
		return false;
	}
	if (defA.cwall_compItems == nullptr || defB.cwall_compItems == nullptr) {
		return defA.cwall_compItems == defB.cwall_compItems;
	}
	for (short i = 0; i < a.nComps; ++i) {
		const API_CWallComponent& ca = (*defA.cwall_compItems)[i];
		const API_CWallComponent& cb = (*defB.cwall_compItems)[i];
		if (ca.buildingMaterial != cb.buildingMaterial ||
			ca.framePen != cb.framePen ||
			ca.flagBits != cb.flagBits ||
			!NearlyEqual (ca.fillThick, cb.fillThick)) {
			return false;
		}
	}
	if (defA.cwall_compLItems != nullptr && defB.cwall_compLItems != nullptr) {
		for (short i = 0; i <= a.nComps; ++i) {
			const API_CWallLineComponent& la = (*defA.cwall_compLItems)[i];
			const API_CWallLineComponent& lb = (*defB.cwall_compLItems)[i];
			if (la.linePen != lb.linePen || la.ltypeInd != lb.ltypeInd) {
				return false;
			}
		}
	}
	return true;
}

static bool AreProfilesEqual (
	const API_ProfileAttrType&	a,
	const API_AttributeDefExt&	defA,
	const API_ProfileAttrType&	b,
	const API_AttributeDefExt&	defB)
{
	if (a.head.flags != b.head.flags ||
		a.wallType != b.wallType ||
		a.beamType != b.beamType ||
		a.coluType != b.coluType ||
		a.handrailType != b.handrailType ||
		a.otherGDLObjectType != b.otherGDLObjectType) {
		return false;
	}
	if (defA.profile_vectorImageItems == nullptr || defB.profile_vectorImageItems == nullptr) {
		return defA.profile_vectorImageItems == defB.profile_vectorImageItems;
	}
	return defA.profile_vectorImageItems->Compare (*defB.profile_vectorImageItems);
}

static void CheckDuplicatedForType (API_AttrTypeID typeID, bool needsDefs, GS::Array<AuditRow>& outRows)
{
	struct LoadedAttrWithDefs {
		SafeLoadedAttr		base;
		API_AttributeDefExt	defs;
		bool				hasDefs = false;

		LoadedAttrWithDefs ()
		{
			BNZeroMemory (&defs, sizeof (API_AttributeDefExt));
		}
	};

	GS::Array<SafeLoadedAttr> baseAttrs;
	LoadAttributesOfType (typeID, baseAttrs);
	if (baseAttrs.GetSize () < 2) {
		return;
	}

	GS::Array<LoadedAttrWithDefs> items;
	for (const SafeLoadedAttr& b : baseAttrs) {
		LoadedAttrWithDefs item;
		item.base = b;
		if (needsDefs) {
			if (ACAPI_Attribute_GetDefExt (typeID, item.base.attr.header.index, &item.defs) == NoError) {
				item.hasDefs = true;
			}
		}
		items.Push (item);
	}

	const USize n = items.GetSize ();
	for (UIndex i = 0; i < n; ++i) {
		for (UIndex j = i + 1; j < n; ++j) {
			bool isDup = false;
			switch (typeID) {
				case API_LinetypeID:
					isDup = AreLinetypesEqual (items[i].base.attr.linetype, items[i].defs, items[j].base.attr.linetype, items[j].defs);
					break;
				case API_FilltypeID:
					isDup = AreFillsEqual (items[i].base.attr.filltype, items[i].defs, items[j].base.attr.filltype, items[j].defs);
					break;
				case API_MaterialID:
					isDup = AreMaterialsEqual (items[i].base.attr.material, items[j].base.attr.material);
					break;
				case API_BuildingMaterialID:
					isDup = AreBuildingMaterialsEqual (items[i].base.attr.buildingMaterial, items[j].base.attr.buildingMaterial);
					break;
				case API_CompWallID:
					isDup = AreCompWallsEqual (items[i].base.attr.compWall, items[i].defs, items[j].base.attr.compWall, items[j].defs);
					break;
				case API_ProfileID:
					isDup = AreProfilesEqual (items[i].base.attr.profile, items[i].defs, items[j].base.attr.profile, items[j].defs);
					break;
				default:
					break;
			}
			if (isDup) {
				AuditRow row;
				row.attrType	= typeID;
				row.attrIndex	= items[j].base.attr.header.index;
				row.cells.Push (GetAttrTypeNameCZ (typeID));
				row.cells.Push (items[i].base.name);
				row.cells.Push (items[j].base.name);
				outRows.Push (row);
			}
		}
	}

	for (UIndex i = 0; i < n; ++i) {
		if (items[i].hasDefs) {
			ACAPI_DisposeAttrDefsHdlsExt (&items[i].defs);
		}
	}
}

static void CheckDuplicatedAttributes (GS::Array<AuditRow>& outRows)
{
	outRows.Clear ();
	CheckDuplicatedForType (API_LinetypeID, true, outRows);
	CheckDuplicatedForType (API_FilltypeID, true, outRows);
	CheckDuplicatedForType (API_MaterialID, false, outRows);
	CheckDuplicatedForType (API_BuildingMaterialID, false, outRows);
	CheckDuplicatedForType (API_CompWallID, true, outRows);
	CheckDuplicatedForType (API_ProfileID, true, outRows);
}

} // namespace

void InitAttributeSubChecks (GS::Array<SubCheckState>& outSubChecks)
{
	outSubChecks.Clear ();

	// 1. Chybějící atributy v atributech
	{
		SubCheckState s;
		s.baseName = GS::UniString (u"Chybějící atributy v atributech");
		s.checked  = true;
		s.columns.Push ({ GS::UniString (u"Typ atributu"), 150 });
		s.columns.Push ({ GS::UniString (u"Název atributu"), 220 });
		s.columns.Push ({ GS::UniString (u"Typ chybějícího atributu"), 180 });
		s.columns.Push ({ GS::UniString (u"Parametr / umístění"), 170 });
		outSubChecks.Push (s);
	}

	// 2. Chybějící textury v povrchových materiálech
	{
		SubCheckState s;
		s.baseName = GS::UniString (u"Chybějící textury v povrchových materiálech");
		s.checked  = true;
		s.columns.Push ({ GS::UniString (u"Název povrchového materiálu"), 350 });
		s.columns.Push ({ GS::UniString (u"Chybějící textura"), 370 });
		outSubChecks.Push (s);
	}

	// 3. Nezaklasifikované atributy
	{
		SubCheckState s;
		s.baseName = GS::UniString (u"Nezaklasifikované atributy");
		s.checked  = true;
		s.columns.Push ({ GS::UniString (u"Typ atributu"), 160 });
		s.columns.Push ({ GS::UniString (u"Název atributu"), 280 });
		s.columns.Push ({ GS::UniString (u"Nevyplněný klasifikační systém"), 280 });
		outSubChecks.Push (s);
	}

	// 4. Duplikované atributy
	{
		SubCheckState s;
		s.baseName = GS::UniString (u"Duplikované atributy");
		s.checked  = true;
		s.columns.Push ({ GS::UniString (u"Typ atributu"), 160 });
		s.columns.Push ({ GS::UniString (u"První atribut"), 280 });
		s.columns.Push ({ GS::UniString (u"Druhý atribut"), 280 });
		outSubChecks.Push (s);
	}
}

void RunAttributeSubCheck (
	AttributeSubCheckId				subCheckId,
	const GS::HashSet<API_Guid>&	disabledClassificationSystems,
	SubCheckState&					inOutState)
{
	switch (subCheckId) {
		case AttributeSubCheckId::MissingInAttributes:
			CheckMissingAttributesInAttributes (inOutState.rows);
			break;
		case AttributeSubCheckId::MissingTextures:
			CheckMissingTexturesInSurfaces (inOutState.rows);
			break;
		case AttributeSubCheckId::UnclassifiedAttributes:
			CheckUnclassifiedAttributes (disabledClassificationSystems, inOutState.rows);
			break;
		case AttributeSubCheckId::DuplicatedAttributes:
			CheckDuplicatedAttributes (inOutState.rows);
			break;
		default:
			inOutState.rows.Clear ();
			break;
	}
	inOutState.hasRun = true;
}
