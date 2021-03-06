/*****************************************************************************
*                                                                            *
*  PrimeSense Sensor 5.0 Alpha                                               *
*  Copyright (C) 2010 PrimeSense Ltd.                                        *
*                                                                            *
*  This file is part of PrimeSense Common.                                   *
*                                                                            *
*  PrimeSense Sensor is free software: you can redistribute it and/or modify *
*  it under the terms of the GNU Lesser General Public License as published  *
*  by the Free Software Foundation, either version 3 of the License, or      *
*  (at your option) any later version.                                       *
*                                                                            *
*  PrimeSense Sensor is distributed in the hope that it will be useful,      *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of            *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the              *
*  GNU Lesser General Public License for more details.                       *
*                                                                            *
*  You should have received a copy of the GNU Lesser General Public License  *
*  along with PrimeSense Sensor. If not, see <http://www.gnu.org/licenses/>. *
*                                                                            *
*****************************************************************************/






//---------------------------------------------------------------------------
// Includes
//---------------------------------------------------------------------------
#include "Registration.h"
#include "XnSensorDepthStream.h"
#include "XnSensor.h"

//---------------------------------------------------------------------------
// Code
//---------------------------------------------------------------------------
XnRegistration::XnRegistration() :
	m_pRegistrationTable(NULL),
	m_pDepthToShiftTable(NULL),
	m_pDevicePrivateData(NULL),
	m_pDepthStream(NULL),
	m_bInitialized(FALSE),
	m_pTempBuffer(NULL),
	m_bD2SAlloc(FALSE)
{

}

inline XnDouble XnRegistrationFunction1000(XnDouble a, XnDouble b, XnDouble c, XnDouble d, XnDouble e, XnDouble f, XnInt16 x, XnInt16 y)
{
	return a*x*x + b*y*y + c*x*y + d*x + e*y + f;
}

inline XnDouble XnXRegistrationFunction1000(XnRegistrationInformation1000& regInfo1000, XnUInt16 nX, XnUInt16 nY, XnUInt32 nXRes, XnUInt32 nYRes)
{
	return XnRegistrationFunction1000(
		regInfo1000.FuncX.dA,
		regInfo1000.FuncX.dB,
		regInfo1000.FuncX.dC,
		regInfo1000.FuncX.dD,
		regInfo1000.FuncX.dE,
		regInfo1000.FuncX.dF,
		nX - nXRes/2, 
		nY - nYRes/2);
}

inline XnDouble XnYRegistrationFunction1000(XnRegistrationInformation1000& regInfo1000, XnUInt16 nX, XnUInt16 nY, XnUInt32 nXRes, XnUInt32 nYRes)
{
	return XnRegistrationFunction1000(
		regInfo1000.FuncY.dA,
		regInfo1000.FuncY.dB,
		regInfo1000.FuncY.dC,
		regInfo1000.FuncY.dD,
		regInfo1000.FuncY.dE,
		regInfo1000.FuncY.dF,
		nX - nXRes/2, 
		nY - nYRes/2);
}

XnStatus XnRegistration::BuildRegTable1000()
{
	XnStatus nRetVal = XN_STATUS_OK;
	
	// take needed parameters to perform registration
	XnRegistrationInformation1000 regInfo1000;
	nRetVal = XnHostProtocolAlgorithmParams(m_pDevicePrivateData, XN_HOST_PROTOCOL_ALGORITHM_REGISTRATION, &regInfo1000, sizeof(regInfo1000), m_pDepthStream->GetResolution(), m_pDepthStream->GetFPS());
	XN_IS_STATUS_OK(nRetVal);
	
	XnUInt16* pRegTable = m_pRegistrationTable;
	XnDouble dDeltaX, dDeltaY;

	XnDouble dNewX = 0,
		dNewY = 0;

	XnUInt64 nDepthXRes = m_pDepthStream->GetXRes();
	XnUInt64 nDepthYRes = m_pDepthStream->GetYRes();

	const XnUInt16 nIllegalValue = XnUInt16(nDepthXRes*4);

	for (XnUInt16 nY = 0; nY < nDepthYRes; nY++)
	{
		for (XnUInt16 nX = 0; nX < nDepthXRes; nX++)
		{
			dDeltaX = XnXRegistrationFunction1000(regInfo1000, nX, nY, nDepthXRes, nDepthYRes);
			dDeltaY = XnYRegistrationFunction1000(regInfo1000, nX, nY, nDepthXRes, nDepthYRes);

			dNewX = (nX + dDeltaX);
			dNewY =  nY + dDeltaY;

			if (dNewY < 1 || dNewY > nDepthYRes)
			{
				dNewY = 1;
				dNewX = nIllegalValue;
			}
			if (dNewX < 1 || dNewX > nDepthXRes)
			{
				dNewX = nIllegalValue;
			}

			dNewX *= XN_REG_X_SCALE;

			*pRegTable = (XnUInt16)dNewX;
			*(pRegTable + 1) = (XnUInt16)dNewY;

			pRegTable += 2;
		}
	}

	m_dShiftFactor = regInfo1000.dBeta;

	return (XN_STATUS_OK);

}

static void incrementalFitting50(XnInt64 dPrev, XnInt64 ddPrev, XnInt64 dddPrev, XnInt64 coeff, XnInt32 betaPrev, XnInt32 dBeta, XnInt64 &dCurr, XnInt64 &ddCurr, XnInt64 &dddCurr, XnInt32 &betaCurr);
static void incrementalFitting50(XnInt64 ddPrev, XnInt64 dddPrev, XnInt64 coeff, XnInt64 &ddCurr, XnInt64 &dddCurr) { XnInt64 dummy1; XnInt32 dummy2; incrementalFitting50(0, ddPrev, dddPrev, coeff, 0, 0, dummy1, ddCurr, dddCurr, dummy2); }
static void incrementalFitting50(XnInt64 dddPrev, XnInt64 coeff, XnInt64 &dddCurr) { XnInt64 dummy1, dummy2; XnInt32 dummy3; incrementalFitting50(0, 0, dddPrev, coeff, 0, 0, dummy1, dummy2, dddCurr, dummy3); }
void incrementalFitting50(XnInt64 dPrev, XnInt64 ddPrev, XnInt64 dddPrev, XnInt64 coeff, XnInt32 betaPrev, XnInt32 dBeta, XnInt64 &dCurr, XnInt64 &ddCurr, XnInt64 &dddCurr, XnInt32 &betaCurr)
{
	dCurr = dPrev+(ddPrev>>6);
	ddCurr = ddPrev+(dddPrev>>8);
	dddCurr = dddPrev+coeff;
	betaCurr = betaPrev+dBeta;
}

XnInt32 GetFieldValueSigned(XnUInt32 regValue, XnInt32 fieldWidth, XnInt32 fieldOffset)
{
	XnInt32 val = (int)(regValue>>fieldOffset);
	val = (val<<(32-fieldWidth))>>(32-fieldWidth);
	return val;
}

void CreateDXDYTablesInternal(XnDouble* RegXTable, XnDouble* RegYTable,
							  XnInt32 resX, XnInt32 resY,
							  XnInt64 AX6, XnInt64 BX6, XnInt64 CX2, XnInt64 DX2,
							  XnInt32 deltaBetaX,
							  XnInt64 AY6, XnInt64 BY6, XnInt64 CY2, XnInt64 DY2,
							  XnInt32 deltaBetaY,
							  XnInt64 dX0, XnInt64 dY0,
							  XnInt64 dXdX0, XnInt64 dXdY0, XnInt64 dYdX0, XnInt64 dYdY0,
							  XnInt64 dXdXdX0, XnInt64 dYdXdX0, XnInt64 dYdXdY0, XnInt64 dXdXdY0,
							  XnInt64 dYdYdX0, XnInt64 dYdYdY0,
							  XnInt32 betaX, XnInt32 betaY)
{
	XnInt32 tOffs = 0;

	for(XnInt32 row = 0 ; row<resY ; row++)
	{
		incrementalFitting50(dXdXdX0, CX2, dXdXdX0);
		incrementalFitting50(dXdX0, dYdXdX0, DX2, dXdX0, dYdXdX0);

		incrementalFitting50(dX0, dYdX0, dYdYdX0, BX6, betaX, 0, dX0, dYdX0, dYdYdX0, betaX);

		XnInt64 coldXdXdX0 = dXdXdX0, coldXdX0 = dXdX0, coldX0 = dX0;
		XnInt32 colBetaX = betaX;

		incrementalFitting50(dXdXdY0, CY2, dXdXdY0);
		incrementalFitting50(dXdY0, dYdXdY0, DY2, dXdY0, dYdXdY0);

		incrementalFitting50(dY0, dYdY0, dYdYdY0, BY6, betaY, deltaBetaY, dY0, dYdY0, dYdYdY0, betaY);

		XnInt64 coldXdXdY0 = dXdXdY0, coldXdY0 = dXdY0, coldY0 = dY0;
		XnInt32 colBetaY = betaY;

		for(XnInt32 col = 0 ; col<resX ; col++, tOffs++)
		{
			RegXTable[tOffs] = coldX0 * (1.0/(1<<17));
			RegYTable[tOffs] = coldY0 * (1.0/(1<<17));

			incrementalFitting50(coldX0, coldXdX0, coldXdXdX0, AX6, colBetaX, deltaBetaX, coldX0, coldXdX0, coldXdXdX0, colBetaX);
			incrementalFitting50(coldY0, coldXdY0, coldXdXdY0, AY6, colBetaY, 0, coldY0, coldXdY0, coldXdXdY0, colBetaY);
		}
	}
}

void CreateDXDYTables (XnDouble* RegXTable, XnDouble* RegYTable,
					   XnUInt32 resX, XnUInt32 resY,
					   XnInt64 AX6, XnInt64 BX6, XnInt64 CX2, XnInt64 DX2,
					   XnUInt32 deltaBetaX,
					   XnInt64 AY6, XnInt64 BY6, XnInt64 CY2, XnInt64 DY2,
					   XnUInt32 deltaBetaY,
					   XnInt64 dX0, XnInt64 dY0,
					   XnInt64 dXdX0, XnInt64 dXdY0, XnInt64 dYdX0, XnInt64 dYdY0,
					   XnInt64 dXdXdX0, XnInt64 dYdXdX0, XnInt64 dYdXdY0, XnInt64 dXdXdY0,
					   XnInt64 dYdYdX0, XnInt64 dYdYdY0,
					   XnUInt32 startingBetaX, XnUInt32 startingBetaY)
{
	dX0 <<= 9;
	dY0 <<= 9;
	dXdX0 <<= 8;
	dXdY0 <<= 8;
	dYdX0 <<= 8;
	dYdY0 <<= 8;
	dXdXdX0 <<= 8;
	dYdXdX0 <<= 8;
	dYdXdY0 <<= 8;
	dXdXdY0 <<= 8;
	dYdYdX0 <<= 8;
	dYdYdY0 <<= 8;
	startingBetaX <<= 7;
	startingBetaY <<= 7;

	CreateDXDYTablesInternal(RegXTable, RegYTable, resX, resY, AX6, BX6, CX2, DX2, deltaBetaX, AY6, BY6, CY2, DY2,
		deltaBetaY, dX0, dY0, dXdX0, dXdY0, dYdX0, dYdY0, dXdXdX0, dYdXdX0, dYdXdY0, dXdXdY0,
		dYdYdX0, dYdYdY0, startingBetaX, startingBetaY);
}

#define RGB_REG_X_RES 640
#define RGB_REG_Y_RES 512
#define XN_DEPTH_XRES 640
#define XN_DEPTH_YRES 480
#define XN_CMOS_VGAOUTPUT_XRES 1280
#define XN_SENSOR_WIN_OFFET_X 1
#define XN_SENSOR_WIN_OFFET_Y 1
#define RGB_REG_X_VAL_SCALE 16
#define S2D_PEL_CONST 10
#define XN_SENSOR_DEPTH_RGB_CMOS_DISTANCE 2.4
#define S2D_CONST_OFFSET 0.375

void BuildDepthToShiftTable(XnUInt16* pDepth2Shift, XnSensorDepthStream* m_pStream)
{
	XnUInt32 nXScale = XN_CMOS_VGAOUTPUT_XRES / XN_DEPTH_XRES;
	XnInt16* pRGBRegDepthToShiftTable = (XnInt16*)pDepth2Shift; 
	XnUInt32 nIndex = 0;
	XnDouble dDepth = 0;

	XnDepthPixel nMaxDepth = m_pStream->GetDeviceMaxDepth();

	XnDouble dPlanePixelSize;
	m_pStream->GetProperty(XN_STREAM_PROPERTY_ZERO_PLANE_PIXEL_SIZE, &dPlanePixelSize);

	XnUInt64 nPlaneDsr;
	XnDouble dPlaneDsr;
	m_pStream->GetProperty(XN_STREAM_PROPERTY_ZERO_PLANE_DISTANCE, &nPlaneDsr);
	dPlaneDsr = nPlaneDsr;

	XnDouble dPelSize = 1.0 / (dPlanePixelSize * nXScale * S2D_PEL_CONST);
	XnDouble dPelDCC = XN_SENSOR_DEPTH_RGB_CMOS_DISTANCE * dPelSize * S2D_PEL_CONST;
	XnDouble dPelDSR = dPlaneDsr * dPelSize * S2D_PEL_CONST;

	memset(pRGBRegDepthToShiftTable, XN_DEVICE_SENSOR_NO_DEPTH_VALUE, nMaxDepth * sizeof(XnInt16));

	for (nIndex = 0; nIndex < nMaxDepth; nIndex++)
	{
		dDepth = nIndex * dPelSize;
		pRGBRegDepthToShiftTable[nIndex] = ((dPelDCC * (dDepth - dPelDSR) / dDepth) + (S2D_CONST_OFFSET)) * RGB_REG_X_VAL_SCALE;
	}
}

XnStatus XnRegistration::BuildRegTable1080()
{
	XnStatus nRetVal = XN_STATUS_OK;
	
	// take needed parameters to perform registration
	XnRegistrationInformation1080 RegData;
	nRetVal = XnHostProtocolAlgorithmParams(m_pDevicePrivateData, XN_HOST_PROTOCOL_ALGORITHM_REGISTRATION, &RegData, sizeof(RegData), m_pDepthStream->GetResolution(), m_pDepthStream->GetFPS());
	XN_IS_STATUS_OK(nRetVal);

	xnOSMemSet(&m_padInfo, 0, sizeof(m_padInfo));
	nRetVal = XnHostProtocolAlgorithmParams(m_pDevicePrivateData, XN_HOST_PROTOCOL_ALGORITHM_PADDING, &m_padInfo, sizeof(m_padInfo), m_pDepthStream->GetResolution(), m_pDepthStream->GetFPS());
	XN_IS_STATUS_OK(nRetVal);

	XN_VALIDATE_ALIGNED_CALLOC(m_pDepthToShiftTable, XnUInt16, m_pDepthStream->GetXRes()*m_pDepthStream->GetYRes(), XN_DEFAULT_MEM_ALIGN);
	m_bD2SAlloc = TRUE;

	BuildDepthToShiftTable(m_pDepthToShiftTable, m_pDepthStream);

	XnDouble* RegXTable = XN_NEW_ARR(XnDouble, RGB_REG_X_RES*RGB_REG_Y_RES);
	XnDouble* RegYTable = XN_NEW_ARR(XnDouble, RGB_REG_X_RES*RGB_REG_Y_RES);

	XnInt16* pRGBRegDepthToShiftTable = (XnInt16*)m_pDepthToShiftTable; 
	XnUInt16 nDepthXRes = XN_DEPTH_XRES;
	XnUInt16 nDepthYRes = XN_DEPTH_YRES;
	XnUInt32 nXScale = XN_CMOS_VGAOUTPUT_XRES / XN_DEPTH_XRES;
	XnDouble* pRegXTable = (XnDouble*)RegXTable;
	XnDouble* pRegYTable = (XnDouble*)RegYTable;
	XnInt16* pRegTable = (XnInt16*)m_pRegistrationTable;
	XnDouble nNewX = 0;
	XnDouble nNewY = 0;

	// Create the dx dy tables
	CreateDXDYTables(RegXTable, RegYTable,
		nDepthXRes,	nDepthYRes,
		GetFieldValueSigned(RegData.nRGS_AX, 32, 0),
		GetFieldValueSigned(RegData.nRGS_BX, 32, 0),
		GetFieldValueSigned(RegData.nRGS_CX, 32, 0),
		GetFieldValueSigned(RegData.nRGS_DX, 32, 0),
		GetFieldValueSigned(RegData.nRGS_DX_BETA_INC, 24, 0),
		GetFieldValueSigned(RegData.nRGS_AY, 32, 0),
		GetFieldValueSigned(RegData.nRGS_BY, 32, 0),
		GetFieldValueSigned(RegData.nRGS_CY, 32, 0),
		GetFieldValueSigned(RegData.nRGS_DY, 32, 0),
		GetFieldValueSigned(RegData.nRGS_DY_BETA_INC, 24, 0),
		GetFieldValueSigned(RegData.nRGS_DX_START, 19, 0),
		GetFieldValueSigned(RegData.nRGS_DY_START, 19, 0),
		GetFieldValueSigned(RegData.nRGS_DXDX_START, 21, 0),
		GetFieldValueSigned(RegData.nRGS_DXDY_START, 21, 0),
		GetFieldValueSigned(RegData.nRGS_DYDX_START, 21, 0),
		GetFieldValueSigned(RegData.nRGS_DYDY_START, 21, 0),
		GetFieldValueSigned(RegData.nRGS_DXDXDX_START, 27, 0),
		GetFieldValueSigned(RegData.nRGS_DYDXDX_START, 27, 0),
		GetFieldValueSigned(RegData.nRGS_DYDXDY_START, 27, 0),
		GetFieldValueSigned(RegData.nRGS_DXDXDY_START, 27, 0),
		GetFieldValueSigned(RegData.nRGS_DYDYDX_START, 27, 0),
		GetFieldValueSigned(RegData.nRGS_DYDYDY_START, 27, 0),
		GetFieldValueSigned(RegData.nRGS_DX_BETA_START, 17, 0),
		GetFieldValueSigned(RegData.nRGS_DY_BETA_START, 17, 0)
		);

	// Pre-process the table, do sanity checks and convert it from double to ints (for better performance)
	for (XnInt32 nY=0; nY<nDepthYRes; nY++)
	{
		for (XnInt32 nX=0; nX<nDepthXRes; nX++)
		{
			nNewX = (nX + *pRegXTable + XN_SENSOR_WIN_OFFET_X) * RGB_REG_X_VAL_SCALE;
			nNewY = (nY + *pRegYTable + XN_SENSOR_WIN_OFFET_Y);

			if (nNewY < 1)
			{
				nNewY = 1;
				nNewX = ((nDepthXRes*4) * RGB_REG_X_VAL_SCALE); // set illegal value on purpose
			}

			if (nNewX < 1)
			{
				nNewX = ((nDepthXRes*4) * RGB_REG_X_VAL_SCALE); // set illegal value on purpose
			}

			if (nNewY > nDepthYRes)
			{
				nNewY = nDepthYRes;
				goto FinishLoop;
			}

			*pRegTable = nNewX;
			*(pRegTable+1) = nNewY;

			pRegXTable++;
			pRegYTable++;
			pRegTable+=2;
		}
	}

FinishLoop:
	XN_DELETE_ARR(RegXTable);
	XN_DELETE_ARR(RegYTable);
	
	return (XN_STATUS_OK);
}

XnStatus XnRegistration::BuildRegTable()
{
	XnStatus nRetVal = XN_STATUS_OK;
	
	m_b1000 = (m_pDevicePrivateData->ChipInfo.nChipVer == XN_SENSOR_CHIP_VER_PS1000);
	if (m_b1000)
	{
		return BuildRegTable1000();
	}
	else
	{
		return BuildRegTable1080();
	}
	
	return (XN_STATUS_OK);
}

XnStatus XnRegistration::Init(XnDevicePrivateData* pDevicePrivateData, XnSensorDepthStream* pDepthStream, XnUInt16* pDepthToShiftTable)
{
	XnStatus nRetVal = XN_STATUS_OK;

	Free();

	m_pDevicePrivateData = pDevicePrivateData;
	m_pDepthStream = pDepthStream;
	m_pDepthToShiftTable = pDepthToShiftTable;

	// allocate table
	XN_VALIDATE_ALIGNED_CALLOC(m_pRegistrationTable, XnUInt16, pDepthStream->GetXRes()*pDepthStream->GetYRes()*2, XN_DEFAULT_MEM_ALIGN);

	// allocate temp buffer
	XN_VALIDATE_ALIGNED_CALLOC(m_pTempBuffer, XnDepthPixel, pDepthStream->GetXRes()*pDepthStream->GetYRes(), XN_DEFAULT_MEM_ALIGN);

	nRetVal = BuildRegTable();
	XN_IS_STATUS_OK(nRetVal);

	m_bInitialized = TRUE;

	return XN_STATUS_OK;
}

XnStatus XnRegistration::Free()
{
	m_bInitialized = FALSE;

	if (m_pRegistrationTable != NULL)
	{
		xnOSFreeAligned(m_pRegistrationTable);
		m_pRegistrationTable = NULL;
	}

	if (m_pTempBuffer != NULL)
	{
		xnOSFreeAligned(m_pTempBuffer);
		m_pTempBuffer = NULL;
	}

	if (m_bD2SAlloc && m_pDepthToShiftTable != NULL)
	{
		xnOSFreeAligned(m_pDepthToShiftTable);
		m_pDepthToShiftTable = NULL;
		m_bD2SAlloc = FALSE;
	}

	return (XN_STATUS_OK);
}

void XnRegistration::Apply(XnDepthPixel* pDM)
{
	XnUInt32 nDepthXRes = m_pDepthStream->GetXRes();
	XnUInt32 nDepthYRes = m_pDepthStream->GetYRes();

	// copy buffer aside
	xnOSMemCopy(m_pTempBuffer, pDM, nDepthXRes*nDepthYRes*sizeof(XnDepthPixel));

	if (m_b1000)
	{
		Apply1000(m_pTempBuffer, pDM);
	}
	else
	{
		Apply1080(m_pTempBuffer, pDM);
	}
}

void XnRegistration::Apply1000(XnDepthPixel* pInput, XnDepthPixel* pOutput)
{
	XnUInt32 nDepthXRes = m_pDepthStream->GetXRes();
	XnUInt32 nDepthYRes = m_pDepthStream->GetYRes();

	XnUInt16* pRegTable = m_pRegistrationTable;
	XnUInt16* pDepth2ShiftTable = m_pDepthToShiftTable;
	XnDepthPixel* pInputEnd = pInput + nDepthYRes*nDepthXRes;
	XnDepthPixel nValue, nOutValue;
	XnInt32 nNewX, nNewY;
	XnUInt32 nArrPos;

	xnOSMemSet(pOutput, 0, m_pDepthStream->GetRequiredDataSize());

	XnDouble dShiftFactor = m_dShiftFactor;
	XnUInt32 nConstShift = m_pDepthStream->GetConstShift();

	while (pInput != pInputEnd)
	{
		nValue = *pInput;

		if (nValue != 0)
		{
			nNewX = (XnInt32)(XnDouble(*pRegTable)/XN_REG_X_SCALE + XnInt32(pDepth2ShiftTable[nValue]/XN_REG_PARAB_COEFF - nConstShift) * dShiftFactor);
			nNewY = *(pRegTable+1);

			if ( nNewX < nDepthXRes && nNewY < nDepthYRes )
			{
				nArrPos = nNewY * nDepthXRes + nNewX;
				nOutValue = pOutput[nArrPos];

				if (nOutValue == 0 || nOutValue > nValue)
				{
					if ( nNewX > 0 && nNewY > 0 )
					{
						pOutput[nArrPos-nDepthXRes] = nValue;
						pOutput[nArrPos-nDepthXRes-1] = nValue;
						pOutput[nArrPos-1] = nValue;
					}
					else if( nNewY > 0 )
					{
						pOutput[nArrPos-nDepthXRes] = nValue;
					}
					else if( nNewX > 0 )
					{
						pOutput[nArrPos-1] = nValue;
					}
					
					pOutput[nArrPos] = nValue;
				}
			}
		}

		pInput++;
		pRegTable += 2;
	}
}

void XnRegistration::Apply1080(XnDepthPixel* pInput, XnDepthPixel* pOutput)
{
	XnInt16* pRegTable = (XnInt16*)m_pRegistrationTable;
	XnInt16* pRGBRegDepthToShiftTable = (XnInt16*)m_pDepthToShiftTable; 
	XnDepthPixel nValue = 0;
	XnDepthPixel nOutValue = 0;
	XnUInt32 nNewX = 0;
	XnUInt32 nNewY = 0;
	XnUInt32 nArrPos = 0;
	XnUInt32 nDepthXRes = XN_DEPTH_XRES;
	XnUInt32 nDepthYRes = XN_DEPTH_YRES;

	memset(pOutput, XN_DEVICE_SENSOR_NO_DEPTH_VALUE, nDepthXRes*nDepthYRes*sizeof(XnDepthPixel));

	// entire map should be shifted by X lines
	XnUInt32 nConstOffset = nDepthYRes*m_padInfo.nStartLines;

	XnDepthPixel* pInputEnd = pInput + nDepthYRes*nDepthXRes;

	XnBool bMirror = m_pDepthStream->IsMirrored();

	for (XnUInt32 y = 0; y < nDepthYRes; ++y)
	{
		pRegTable = (XnInt16*)&m_pRegistrationTable[ bMirror ? (y+1) * nDepthXRes * 2 - 2 : y * nDepthXRes * 2 ];
		for (XnUInt32 x = 0; x < nDepthXRes; ++x)
		{
			nValue = *pInput;

			if (nValue != XN_DEVICE_SENSOR_NO_DEPTH_VALUE)
			{
				nNewX = (XnUInt32)(*pRegTable + pRGBRegDepthToShiftTable[nValue]) / RGB_REG_X_VAL_SCALE;
				nNewY = *(pRegTable+1);

				if (nNewX < nDepthXRes && nNewY < nDepthYRes )
				{
					nArrPos = bMirror ? (nNewY+1)*nDepthXRes - nNewX - 2 : (nNewY*nDepthXRes) + nNewX;
					nArrPos -= nConstOffset;
					
					nOutValue = pOutput[nArrPos];

					if ((nOutValue == XN_DEVICE_SENSOR_NO_DEPTH_VALUE) || (nOutValue > nValue))
					{
						if ( nNewX > 0 && nNewY > 0 )
						{
							pOutput[nArrPos-nDepthXRes] = nValue;
							pOutput[nArrPos-nDepthXRes-1] = nValue;
							pOutput[nArrPos-1] = nValue;
						}
						else if( nNewY > 0 )
						{
							pOutput[nArrPos-nDepthXRes] = nValue;
						}
						else if( nNewX > 0 )
						{
							pOutput[nArrPos-1] = nValue;
						}
						
						pOutput[nArrPos] = nValue;
					}
				}
			}

			pInput++;
			bMirror ? pRegTable-=2 : pRegTable+=2;
		}
	}
}
