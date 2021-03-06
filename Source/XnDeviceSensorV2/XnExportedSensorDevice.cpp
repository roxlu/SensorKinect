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
#include "XnExportedSensorDevice.h"
#include <XnPsVersion.h>
#include "XnSensorDevice.h"
#include <XnOpenNI.h>
#include <XnCommon/XnCommon.h>
#include "XnSensorServer.h"

//---------------------------------------------------------------------------
// XnExportedSensorDevice class
//---------------------------------------------------------------------------
XnExportedSensorDevice::XnExportedSensorDevice()
{}

void XnExportedSensorDevice::FillCommonDescriptionFields(XnProductionNodeDescription* pDescription)
{
	strcpy(pDescription->strName, XN_DEVICE_NAME);
	strcpy(pDescription->strVendor, XN_VENDOR_PRIMESENSE);
	
	pDescription->Version.nMajor = XN_PS_MAJOR_VERSION;
	pDescription->Version.nMinor = XN_PS_MINOR_VERSION;
	pDescription->Version.nMaintenance = XN_PS_MAINTENANCE_VERSION;
	pDescription->Version.nBuild = XN_PS_BUILD_VERSION;
}

void XnExportedSensorDevice::GetDescription(XnProductionNodeDescription* pDescription)
{
	FillCommonDescriptionFields(pDescription);
	pDescription->Type = XN_NODE_TYPE_DEVICE;
}

XnStatus XnExportedSensorDevice::EnumerateProductionTrees(xn::Context& context, xn::NodeInfoList& TreesList, xn::EnumerationErrors* pErrors)
{
	XnStatus nRetVal = XN_STATUS_OK;
	
	// enumerate connected sensors
	XnUInt32 nCount = 0;
	
	// check if sensor is connected
	nRetVal = XnSensor::Enumerate(NULL, &nCount);
	if (nRetVal != XN_STATUS_OUTPUT_BUFFER_OVERFLOW)
	{
		// no sensor connected
		XN_LOG_WARNING_RETURN(XN_STATUS_DEVICE_NOT_CONNECTED, XN_MASK_DEVICE_SENSOR, "No PS sensor is connected!");
	}
	
	// allocate according to count
	XnConnectionString* pConnStrings;
	XN_VALIDATE_CALLOC(pConnStrings, XnConnectionString, nCount);
	
	nRetVal = XnSensor::Enumerate(pConnStrings, &nCount);
	if (nRetVal != XN_STATUS_OK)
	{
		xnOSFree(pConnStrings);
		return (nRetVal);
	}
	
	XnProductionNodeDescription Description;
	GetDescription(&Description);
	
	// each connection string is a sensor. return it
	for (XnUInt32 i = 0; i < nCount; ++i)
	{
		nRetVal = TreesList.Add(Description, pConnStrings[i], NULL);
		if (nRetVal != XN_STATUS_OK)
		{
			xnOSFree(pConnStrings);
			return (nRetVal);
		}
	}
	
	xnOSFree(pConnStrings);
	
	return (XN_STATUS_OK);
}

XnStatus XnExportedSensorDevice::Create(xn::Context& context, const XnChar* strInstanceName, const XnChar* strCreationInfo, xn::NodeInfoList* pNeededTrees, const XnChar* strConfigurationDir, xn::ModuleProductionNode** ppInstance)
{
	XnStatus nRetVal = XN_STATUS_OK;
	
	XnChar strGlobalConfigFile[XN_FILE_MAX_PATH];
	nRetVal = XnSensor::ResolveGlobalConfigFileName(strGlobalConfigFile, XN_FILE_MAX_PATH, strConfigurationDir);
	XN_IS_STATUS_OK(nRetVal);
	
#if (XN_PLATFORM == XN_PLATFORM_MACOSX)
	XnBool bEnableMultiProcess = FALSE;
#else
	
	XnBool bEnableMultiProcess = TRUE;
	XnUInt32 nValue;
	if (XN_STATUS_OK == xnOSReadIntFromINI(strGlobalConfigFile, XN_CONFIG_FILE_SERVER_SECTION, XN_MODULE_PROPERTY_ENABLE_MULTI_PROCESS, &nValue))
	{
		bEnableMultiProcess = (nValue == TRUE);
	}
#endif
	
	XnDeviceBase* pSensor = NULL;
	
	if (bEnableMultiProcess)
	{
		XN_VALIDATE_NEW(pSensor, XnSensorClient);
	}
	else
	{
		XN_VALIDATE_NEW(pSensor, XnSensor);
	}
	
	XnDeviceConfig config;
	config.DeviceMode = XN_DEVICE_MODE_READ;
	config.cpConnectionString = strCreationInfo;
	config.SharingMode = XN_DEVICE_EXCLUSIVE;
	config.pInitialValues = NULL;
	
	if (strConfigurationDir != NULL)
	{
		if (bEnableMultiProcess)
		{
			XnSensorClient* pClient = (XnSensorClient*)pSensor;
			pClient->SetConfigDir(strConfigurationDir);
		}
		else
		{
			XnSensor* pActualSensor = (XnSensor*)pSensor;
			pActualSensor->SetGlobalConfigFile(strGlobalConfigFile);
		}
	}
	
	nRetVal = pSensor->Init(&config);
	if (nRetVal != XN_STATUS_OK)
	{
		XN_DELETE(pSensor);
		return (nRetVal);
	}
	
	XnSensorDevice* pDevice = XN_NEW(XnSensorDevice, context, pSensor, strInstanceName);
	if (pDevice == NULL)
	{
		XN_DELETE(pSensor);
		return (XN_STATUS_ALLOC_FAILED);
	}
	
	nRetVal = pDevice->Init();
	if (nRetVal != XN_STATUS_OK)
	{
		XN_DELETE(pSensor);
		return (nRetVal);
	}
	
	*ppInstance = pDevice;
	
	return (XN_STATUS_OK);
}

void XnExportedSensorDevice::Destroy(xn::ModuleProductionNode* pInstance)
{
	XnSensorDevice* pDevice = dynamic_cast<XnSensorDevice*>(pInstance);
	XnDeviceBase* pSensor = pDevice->GetSensor();
	pSensor->Destroy();
	XN_DELETE(pSensor);
	XN_DELETE(pDevice);
}
