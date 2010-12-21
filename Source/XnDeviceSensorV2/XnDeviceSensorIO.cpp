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
#include "XnDeviceSensorIO.h"
#include "XnDeviceSensor.h"

//---------------------------------------------------------------------------
// Defines
//---------------------------------------------------------------------------
#define XN_SENSOR_VENDOR_ID			0x1D27
#define XN_SENSOR_VENDOR_ID_KINECT	0x045E
#define XN_SENSOR_2_0_PRODUCT_ID	0x0200
#define XN_SENSOR_5_0_PRODUCT_ID	0x0500
#define XN_SENSOR_6_0_PRODUCT_ID	0x0600
#define XN_SENSOR_KINECT_PRODUCT_ID	0x02AE

#if XN_PLATFORM == XN_PLATFORM_WIN32
	#include <initguid.h>
	DEFINE_GUID(GUID_CLASS_PSDRV_USB, 0xc3b5f022, 0x5a42, 0x1980, 0x19, 0x09, 0xea, 0x72, 0x09, 0x56, 0x01, 0xb1);
	#define USB_DEVICE_EXTRA_PARAM (void*)&GUID_CLASS_PSDRV_USB
#else
	#define USB_DEVICE_EXTRA_PARAM NULL
#endif

//---------------------------------------------------------------------------
// Enums
//---------------------------------------------------------------------------
typedef enum
{
	XN_FW_USB_INTERFACE_ISO = 0,
	XN_FW_USB_INTERFACE_BULK = 1,
} XnFWUsbInterface;

//---------------------------------------------------------------------------
// Code
//---------------------------------------------------------------------------

XnSensorIO::XnSensorIO(XN_SENSOR_HANDLE* pSensorHandle) :
	m_pSensorHandle(pSensorHandle),
	m_bMiscSupported(FALSE)
{
}

XnSensorIO::~XnSensorIO()
{

}

XnStatus XnSensorIO::OpenDevice(const XnChar* strPath)
{
	XnStatus nRetVal;
	XnUSBDeviceSpeed DevSpeed;

	nRetVal = xnUSBInit();
	if (nRetVal != XN_STATUS_OK && nRetVal != XN_STATUS_USB_ALREADY_INIT)
		return nRetVal;

	xnLogVerbose(XN_MASK_DEVICE_IO, "Connecting to USB device...");

	if (strstr(strPath, "\\\\?\\usb") == NULL)
	{
		strPath = NULL;
	}

	// try to open a 6.0 device
	xnLogVerbose(XN_MASK_DEVICE_IO, "Trying to open a 6.0 sensor...");
	nRetVal = xnUSBOpenDevice(XN_SENSOR_VENDOR_ID, XN_SENSOR_6_0_PRODUCT_ID, USB_DEVICE_EXTRA_PARAM, (void*)strPath, &m_pSensorHandle->USBDevice);
	if (nRetVal == XN_STATUS_USB_DEVICE_NOT_FOUND)
	{
		// if not found, see if we have a 5.0 device
		xnLogVerbose(XN_MASK_DEVICE_IO, "Can't find 6.0. Trying to open a 5.0 sensor...");
		nRetVal = xnUSBOpenDevice(XN_SENSOR_VENDOR_ID, XN_SENSOR_5_0_PRODUCT_ID, USB_DEVICE_EXTRA_PARAM, (void*)strPath, &m_pSensorHandle->USBDevice);
	}
	if (nRetVal == XN_STATUS_USB_DEVICE_NOT_FOUND)
	{
		// if not found, see if we have a 2.0 - 4.0 devices
		xnLogVerbose(XN_MASK_DEVICE_IO, "Can't find 5.0. Trying to open an older sensor...");
		nRetVal = xnUSBOpenDevice(XN_SENSOR_VENDOR_ID, XN_SENSOR_2_0_PRODUCT_ID, USB_DEVICE_EXTRA_PARAM, (void*)strPath, &m_pSensorHandle->USBDevice);
	}
	if (nRetVal == XN_STATUS_USB_DEVICE_NOT_FOUND)
	{
		// if not found, try the kinect
		xnLogVerbose(XN_MASK_DEVICE_IO, "Can't find 2.0 - 4.0. Trying to open a kinect sensor...");
		nRetVal = xnUSBOpenDevice(XN_SENSOR_VENDOR_ID_KINECT, XN_SENSOR_KINECT_PRODUCT_ID, USB_DEVICE_EXTRA_PARAM, (void*)strPath, &m_pSensorHandle->USBDevice);
	}	

	XN_IS_STATUS_OK(nRetVal);

	nRetVal = xnUSBGetDeviceSpeed(m_pSensorHandle->USBDevice, &DevSpeed);
	XN_IS_STATUS_OK(nRetVal);

	if (DevSpeed != XN_USB_DEVICE_HIGH_SPEED)
	{
		XN_LOG_WARNING_RETURN(XN_STATUS_USB_UNKNOWN_DEVICE_SPEED, XN_MASK_DEVICE_IO, "Device is not high speed!");
	}

	// on older firmwares, control was sent over BULK endpoints. Check if this is the case
	xnLogVerbose(XN_MASK_DEVICE_IO, "Trying to open endpoint 0x4 for control out (for old firmwares)...");
	nRetVal = xnUSBOpenEndPoint(m_pSensorHandle->USBDevice, 0x4, XN_USB_EP_BULK, XN_USB_DIRECTION_OUT, &m_pSensorHandle->ControlConnection.ControlOutConnectionEp);
	if (nRetVal == XN_STATUS_USB_ENDPOINT_NOT_FOUND || nRetVal == XN_STATUS_USB_WRONG_ENDPOINT_TYPE || nRetVal == XN_STATUS_USB_WRONG_ENDPOINT_DIRECTION)
	{
		// this is not the case. use regular control endpoint (0)
		m_pSensorHandle->ControlConnection.bIsBulk = FALSE;
	}
	else
	{
		XN_IS_STATUS_OK(nRetVal);

		xnLogVerbose(XN_MASK_DEVICE_IO, "Opening endpoint 0x85 for control in...");
		nRetVal = xnUSBOpenEndPoint(m_pSensorHandle->USBDevice, 0x85, XN_USB_EP_BULK, XN_USB_DIRECTION_IN, &m_pSensorHandle->ControlConnection.ControlInConnectionEp);
		XN_IS_STATUS_OK(nRetVal);

		m_pSensorHandle->ControlConnection.bIsBulk = TRUE;
	}

	xnLogInfo(XN_MASK_DEVICE_IO, "Connected to USB device");

	return XN_STATUS_OK;
}

XnStatus XnSensorIO::OpenDataEndPoints(XnSensorUsbInterface nInterface)
{
	XnStatus nRetVal = XN_STATUS_OK;

	// try to set requested interface
	if (nInterface != XN_SENSOR_USB_INTERFACE_DEFAULT)
	{
		XnFWUsbInterface nFWInterface;
		switch (nInterface)
		{
		case XN_SENSOR_USB_INTERFACE_ISO_ENDPOINTS:
			nFWInterface = XN_FW_USB_INTERFACE_ISO;
			break;
		case XN_SENSOR_USB_INTERFACE_BULK_ENDPOINTS:
			nFWInterface = XN_FW_USB_INTERFACE_BULK;
			break;
		default:
			XN_LOG_WARNING_RETURN(XN_STATUS_USB_INTERFACE_NOT_SUPPORTED, XN_MASK_DEVICE_IO, "Unknown interface type: %d", nInterface);
		}

		xnLogVerbose(XN_MASK_DEVICE_IO, "Setting USB interface to %d...", nFWInterface);
		nRetVal = xnUSBSetInterface(m_pSensorHandle->USBDevice, 0, nFWInterface);
		XN_IS_STATUS_OK(nRetVal);
	}

	xnLogVerbose(XN_MASK_DEVICE_IO, "Opening endpoints...");

	// up until v3.0/4.0, Image went over 0x82, depth on 0x83, audio on 0x86, and control was using bulk EPs, at 0x4 and 0x85.
	// starting v3.0/4.0, Image is at 0x81, depth at 0x82, audio/misc at 0x83, and control is using actual control EPs.
	// This means we are using the new Jungo USB Code
	XnBool bNewUSB = TRUE;

	// Depth
	m_pSensorHandle->DepthConnection.bIsISO = FALSE;

	xnLogVerbose(XN_MASK_DEVICE_IO, "Opening endpoint 0x81 for depth...");
	nRetVal = xnUSBOpenEndPoint(m_pSensorHandle->USBDevice, 0x81, XN_USB_EP_BULK, XN_USB_DIRECTION_IN, &m_pSensorHandle->DepthConnection.UsbEp);
	if (nRetVal == XN_STATUS_USB_ENDPOINT_NOT_FOUND)
	{
		bNewUSB = FALSE;
		xnLogVerbose(XN_MASK_DEVICE_IO, "Endpoint 0x81 does not exist. Trying old USB: Opening 0x82 for depth...");
		nRetVal = xnUSBOpenEndPoint(m_pSensorHandle->USBDevice, 0x82, XN_USB_EP_BULK, XN_USB_DIRECTION_IN, &m_pSensorHandle->DepthConnection.UsbEp);
		XN_IS_STATUS_OK(nRetVal);
	}
	else
	{
		if (nRetVal == XN_STATUS_USB_WRONG_ENDPOINT_TYPE)
		{
			nRetVal = xnUSBOpenEndPoint(m_pSensorHandle->USBDevice, 0x81, XN_USB_EP_ISOCHRONOUS, XN_USB_DIRECTION_IN, &m_pSensorHandle->DepthConnection.UsbEp);

			m_pSensorHandle->DepthConnection.bIsISO = TRUE;
		}

		bNewUSB = TRUE;

		XN_IS_STATUS_OK(nRetVal);

		if (m_pSensorHandle->DepthConnection.bIsISO  == TRUE)
		{
			xnLogVerbose(XN_MASK_DEVICE_IO, "Depth endpoint is isochronous.");
		}
		else
		{
			xnLogVerbose(XN_MASK_DEVICE_IO, "Depth endpoint is bulk.");
		}
	}
	m_pSensorHandle->DepthConnection.bIsOpen = TRUE;

	nRetVal = xnUSBGetEndPointMaxPacketSize(m_pSensorHandle->DepthConnection.UsbEp, &m_pSensorHandle->DepthConnection.nMaxPacketSize);
	XN_IS_STATUS_OK(nRetVal);

	// check this matches requested interface (unless DEFAULT was requested)
	if (nInterface == XN_SENSOR_USB_INTERFACE_BULK_ENDPOINTS && m_pSensorHandle->DepthConnection.bIsISO ||
		nInterface == XN_SENSOR_USB_INTERFACE_ISO_ENDPOINTS && !m_pSensorHandle->DepthConnection.bIsISO)
	{
		return (XN_STATUS_USB_INTERFACE_NOT_SUPPORTED);
	}

	m_interface = m_pSensorHandle->DepthConnection.bIsISO ? XN_SENSOR_USB_INTERFACE_ISO_ENDPOINTS : XN_SENSOR_USB_INTERFACE_BULK_ENDPOINTS;

	// Image
	XnUInt16 nImageEP = bNewUSB ? 0x82 : 0x83;

	m_pSensorHandle->ImageConnection.bIsISO = FALSE;

	xnLogVerbose(XN_MASK_DEVICE_IO, "Opening endpoint 0x%hx for image...", nImageEP);
	nRetVal = xnUSBOpenEndPoint(m_pSensorHandle->USBDevice, nImageEP, XN_USB_EP_BULK, XN_USB_DIRECTION_IN, &m_pSensorHandle->ImageConnection.UsbEp);
	if (nRetVal == XN_STATUS_USB_WRONG_ENDPOINT_TYPE)
	{
		nRetVal = xnUSBOpenEndPoint(m_pSensorHandle->USBDevice, nImageEP, XN_USB_EP_ISOCHRONOUS, XN_USB_DIRECTION_IN, &m_pSensorHandle->ImageConnection.UsbEp);

		m_pSensorHandle->ImageConnection.bIsISO = TRUE;
	}

	XN_IS_STATUS_OK(nRetVal);

	if (m_pSensorHandle->ImageConnection.bIsISO  == TRUE)
	{
		xnLogVerbose(XN_MASK_DEVICE_IO, "Image endpoint is isochronous.");
	}
	else
	{
		xnLogVerbose(XN_MASK_DEVICE_IO, "Image endpoint is bulk.");
	}

	m_pSensorHandle->ImageConnection.bIsOpen = TRUE;

	nRetVal = xnUSBGetEndPointMaxPacketSize(m_pSensorHandle->ImageConnection.UsbEp, &m_pSensorHandle->ImageConnection.nMaxPacketSize);
	XN_IS_STATUS_OK(nRetVal);

	// Misc
	XnUInt16 nMiscEP = bNewUSB ? 0x83 : 0x86;

	m_pSensorHandle->MiscConnection.bIsISO = FALSE;

	xnLogVerbose(XN_MASK_DEVICE_IO, "Opening endpoint 0x%hx for misc...", nMiscEP);
	nRetVal = xnUSBOpenEndPoint(m_pSensorHandle->USBDevice, nMiscEP, XN_USB_EP_BULK, XN_USB_DIRECTION_IN, &m_pSensorHandle->MiscConnection.UsbEp);
	if (nRetVal == XN_STATUS_USB_WRONG_ENDPOINT_TYPE)
	{
		nRetVal = xnUSBOpenEndPoint(m_pSensorHandle->USBDevice, nMiscEP, XN_USB_EP_ISOCHRONOUS, XN_USB_DIRECTION_IN, &m_pSensorHandle->MiscConnection.UsbEp);

		m_pSensorHandle->MiscConnection.bIsISO = TRUE;
	}
	if (nRetVal == XN_STATUS_USB_ENDPOINT_NOT_FOUND)
	{
		// Firmware does not support misc...
		m_pSensorHandle->MiscConnection.bIsOpen = FALSE;
		m_bMiscSupported = FALSE;

		xnLogVerbose(XN_MASK_DEVICE_IO, "Misc endpoint is not supported...");
	}
	else if (nRetVal == XN_STATUS_OK)
	{
		m_pSensorHandle->MiscConnection.bIsOpen = TRUE;
		m_bMiscSupported = TRUE;

		if (m_pSensorHandle->MiscConnection.bIsISO  == TRUE)
		{ 
			xnLogVerbose(XN_MASK_DEVICE_IO, "Misc endpoint is isochronous.");
		}
		else
		{
			xnLogVerbose(XN_MASK_DEVICE_IO, "Misc endpoint is bulk.");
		}
	}
	else
	{
		return nRetVal;
	}

	if (m_pSensorHandle->MiscConnection.bIsOpen)
	{
		nRetVal = xnUSBGetEndPointMaxPacketSize(m_pSensorHandle->MiscConnection.UsbEp, &m_pSensorHandle->MiscConnection.nMaxPacketSize);
		XN_IS_STATUS_OK(nRetVal);
	}

	xnLogInfo(XN_MASK_DEVICE_IO, "Endpoints open");

	// All is good...
	return (XN_STATUS_OK);
}

XnStatus XnSensorIO::CloseDevice()
{
	XnStatus nRetVal;

	xnLogVerbose(XN_MASK_DEVICE_SENSOR, "Shutting down USB depth read thread...");
	xnUSBShutdownReadThread(m_pSensorHandle->DepthConnection.UsbEp);

	if (m_pSensorHandle->DepthConnection.UsbEp != NULL)
	{
		nRetVal = xnUSBCloseEndPoint(m_pSensorHandle->DepthConnection.UsbEp);
		XN_IS_STATUS_OK(nRetVal);
		m_pSensorHandle->DepthConnection.UsbEp = NULL;
	}

	xnLogVerbose(XN_MASK_DEVICE_SENSOR, "Shutting down USB image read thread...");
	xnUSBShutdownReadThread(m_pSensorHandle->ImageConnection.UsbEp);

	if (m_pSensorHandle->ImageConnection.UsbEp != NULL)
	{
		nRetVal = xnUSBCloseEndPoint(m_pSensorHandle->ImageConnection.UsbEp);
		XN_IS_STATUS_OK(nRetVal);
		m_pSensorHandle->ImageConnection.UsbEp = NULL;
	}

	if (m_pSensorHandle->MiscConnection.bIsOpen)
	{
		xnLogVerbose(XN_MASK_DEVICE_SENSOR, "Shutting down USB misc read thread...");
		xnUSBShutdownReadThread(m_pSensorHandle->MiscConnection.UsbEp);

		if (m_pSensorHandle->MiscConnection.UsbEp != NULL)
		{
			nRetVal = xnUSBCloseEndPoint(m_pSensorHandle->MiscConnection.UsbEp);
			XN_IS_STATUS_OK(nRetVal);
			m_pSensorHandle->MiscConnection.UsbEp = NULL;
		}
	}

	if (m_pSensorHandle->ControlConnection.bIsBulk)
	{
		if (m_pSensorHandle->ControlConnection.ControlInConnectionEp != NULL)
		{
			nRetVal = xnUSBCloseEndPoint(m_pSensorHandle->ControlConnection.ControlInConnectionEp);
			XN_IS_STATUS_OK(nRetVal);
			m_pSensorHandle->ControlConnection.ControlInConnectionEp = NULL;
		}

		if (m_pSensorHandle->ControlConnection.ControlOutConnectionEp != NULL)
		{
			nRetVal = xnUSBCloseEndPoint(m_pSensorHandle->ControlConnection.ControlOutConnectionEp);
			XN_IS_STATUS_OK(nRetVal);
			m_pSensorHandle->ControlConnection.ControlOutConnectionEp = NULL;
		}
	}

	if (m_pSensorHandle->USBDevice != NULL)
	{
		nRetVal = xnUSBCloseDevice(m_pSensorHandle->USBDevice);
		XN_IS_STATUS_OK(nRetVal);
		m_pSensorHandle->USBDevice = NULL;
	}

	xnLogVerbose(XN_MASK_DEVICE_SENSOR, "Device closed successfully");

	// All is good...
	return (XN_STATUS_OK);
}

XnStatus XnSensorIO::GetNumOfSensors(XnUInt32* pnNumSensors)
{
	XnStatus nRetVal = XN_STATUS_OK;
	XnBool bIsPresent = FALSE;

	 *pnNumSensors = 0;

	nRetVal = xnUSBInit();
	if (nRetVal != XN_STATUS_OK && nRetVal != XN_STATUS_USB_ALREADY_INIT)
		return nRetVal;

	// search for a v6.0 device
	nRetVal = xnUSBIsDevicePresent(XN_SENSOR_VENDOR_ID, XN_SENSOR_6_0_PRODUCT_ID, USB_DEVICE_EXTRA_PARAM, &bIsPresent);
	XN_IS_STATUS_OK(nRetVal);

	if (!bIsPresent)
	{
		// search for a v5.0 device
		nRetVal = xnUSBIsDevicePresent(XN_SENSOR_VENDOR_ID, XN_SENSOR_5_0_PRODUCT_ID, USB_DEVICE_EXTRA_PARAM, &bIsPresent);
		XN_IS_STATUS_OK(nRetVal);
	}

	if (!bIsPresent)
	{
		// try searching for an older device
		nRetVal = xnUSBIsDevicePresent(XN_SENSOR_VENDOR_ID, XN_SENSOR_2_0_PRODUCT_ID, USB_DEVICE_EXTRA_PARAM, &bIsPresent);
		XN_IS_STATUS_OK(nRetVal);
	}
	
	if (!bIsPresent)
	{
		// try searching for a kinect
		nRetVal = xnUSBIsDevicePresent(XN_SENSOR_VENDOR_ID_KINECT, XN_SENSOR_KINECT_PRODUCT_ID, USB_DEVICE_EXTRA_PARAM, &bIsPresent);
		XN_IS_STATUS_OK(nRetVal);
	}	

	if (bIsPresent == TRUE)
	{
		*pnNumSensors = 1;
	}

	// All is good...
	return (XN_STATUS_OK);
}

XnStatus XnSensorIO::SetCallback(XnUSBEventCallbackFunctionPtr pCallbackPtr, void* pCallbackData)
{
	//TODO: Support multiple sensors - this won't work for more than one.
	XnStatus nRetVal = XN_STATUS_OK;
	
	// try to register callback to a 5.0 device
	nRetVal = xnUSBSetCallbackHandler(XN_SENSOR_VENDOR_ID, XN_SENSOR_5_0_PRODUCT_ID, USB_DEVICE_EXTRA_PARAM, pCallbackPtr, pCallbackData);
	if (nRetVal == XN_STATUS_USB_DEVICE_NOT_FOUND)
	{
		// if not found, see if we have a 2.0 - 4.0 devices
		nRetVal = xnUSBSetCallbackHandler(XN_SENSOR_VENDOR_ID, XN_SENSOR_2_0_PRODUCT_ID, USB_DEVICE_EXTRA_PARAM, pCallbackPtr, pCallbackData);
	}

	return nRetVal;
}
