// ===============================================================================================
// =                                UAVX Quadrocopter Controller                                 =
// =                           Copyright (c) 2008 by Prof. Greg Egan                             =
// =                 Original V3.15 Copyright (c) 2007 Ing. Wolfgang Mahringer                   =
// =                     http://code.google.com/p/uavp-mods/ http://uavp.ch                      =
// ===============================================================================================

//    This is part of UAVX.

//    UAVX is free software: you can redistribute it and/or modify it under the terms of the GNU 
//    General Public License as published by the Free Software Foundation, either version 3 of the 
//    License, or (at your option) any later version.

//    UAVX is distributed in the hope that it will be useful,but WITHOUT ANY WARRANTY; without
//    even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
//    See the GNU General Public License for more details.

//    You should have received a copy of the GNU General Public License along with this program.  
//    If not, see http://www.gnu.org/licenses/

#include "UAVX.h"

#define TELEMETRY_FLAG_BYTES  6

enum RxPacketStates {
	WaitRxSentinel,
	WaitRxBody,
	WaitRxESC,
	WaitRxCheck,
	WaitRxCopy,
	WaitUPTag,
	WaitUPLength,
	WaitUPBody,
	WaitRxTag
};

uint16 PacketsReceived[128] = { 0, };
uint8 ReceivedPacketTag, RxPacketTag, PacketRxState, RxPacketLength,
		RxPacketByteCount;
boolean PacketReceived, RxPacketError, CheckSumError;
uint8 UAVXPacket[256];
uint16 RxLengthErrors = 0;
uint16 RxCheckSumErrors = 0;

uint8 TxPacketTag;
uint8 CurrTelType = UAVXTelemetry;

void TxNextLine(uint8 s) {
	TxChar(s, ASCII_CR);
	TxChar(s, ASCII_LF);
} // TxNextLine

void TxString(uint8 s, const char *pch) {
	while (*pch != (char) 0)
		TxChar(s, *pch++);
} // TxString

void TxNibble(uint8 s, uint8 v) {
	if (v > 9)
		TxChar(s, 'a' + v - 10);
	else
		TxChar(s, '0' + v);
} // TxNibble

void TxValH(uint8 s, uint8 v) {
	TxNibble(s, v >> 4);
	TxNibble(s, v & 0x0f);
} // TxValH

void TxValH16(uint8 s, int16 v) {
	TxValH(s, v >> 8);
	TxValH(s, v);
} // TxValH16

void TxBin8(uint8 s, int8 v) {
	TxChar(s, v);
} // TxBin8

void TxBin16(uint8 s, int16 v) {
	TxChar(s, v);
	TxChar(s, v >> 8);
} // TxBin16

void TxBin32(uint8 s, int32 v) {
	TxBin16(s, v);
	TxBin16(s, v >> 16);
} // TxBin16

void TxVal32(uint8 s, int32 V, int8 dp, uint8 Separator) {
	uint8 S[16];
	int8 c, Rem, zeros, i;
	int32 NewV;

	if (V < 0) {
		TxChar(s, '-');
		V = -V;
	}
	//	else
	//		TxChar(s, ' ');

	c = 0;
	do {
		NewV = V / 10;
		Rem = V - (NewV * 10);
		S[c++] = Rem + '0';
		V = NewV;
	} while (V > 0);

	if ((c < (dp + 1)) && (dp > 0)) {
		TxChar(s, '0');
		TxChar(s, '.');
	}

	zeros = (int8) dp - c - 1;
	if (zeros >= 0)
		for (i = zeros; i >= 0; i--)
			TxChar(s, '0');

	do {
		c--;
		TxChar(s, S[c]);
		if ((c == dp) && (c > 0))
			TxChar(s, '.');
	} while (c > 0);

	if (Separator != ASCII_NUL)
		TxChar(s, Separator);
} // TxVal32

void TxESCu8(uint8 s, uint8 ch) {
	if ((ch == ASCII_SOH) || (ch == ASCII_EOT) || (ch == ASCII_ESC))
		TxChar(s, ASCII_ESC);
	TxChar(s, ch);
} // TxESCu8

void TxESCi8(uint8 s, int8 b) {
	if ((b == ASCII_SOH) || (b == ASCII_EOT) || (b == ASCII_ESC))
		TxChar(s, ASCII_ESC);
	TxChar(s, b);
} // TxESCu8

void TxESCi16(uint8 s, int16 v) {
	TxESCu8(s, v & 0xff);
	TxESCu8(s, (v >> 8) & 0xff);
} // TxESCi16

void TxESCi24(uint8 s, int32 v) {
	TxESCu8(s, v & 0xff);
	TxESCu8(s, (v >> 8) & 0xff);
	TxESCu8(s, (v >> 16) & 0xff);
} // TxESCi24

void TxESCi32(uint8 s, int32 v) {
	TxESCi16(s, v & 0xffff);
	TxESCi16(s, (v >> 16) & 0xffff);
} // TxESCi32

uint8 UAVXPacketu8(uint8 p) {
	return UAVXPacket[p];
} // UAVXPacketu8

int16 UAVXPacketi8(uint8 p) {
	int16 temp;

	temp = (int8) UAVXPacket[p];
	//if (temp > 127)
	//	temp -= 256;

	return temp;
} // UAVXPacketi8

int16 UAVXPacketi16(uint8 p) {
	int16 temp;

	temp = (int16) (UAVXPacket[p + 1] << 8);
	temp |= (int16) UAVXPacket[p];

	return temp;
} // UAVXPacketi16

int32 UAVXPacketi24(uint8 p) {
	int32 temp;

	temp = ((int32) UAVXPacket[p + 2] << 24);
	temp |= ((int32) UAVXPacket[p + 1] << 16);
	temp |= (int32) UAVXPacket[p] << 8;
	temp /= 256;
	return temp;
} // UAVXPacketi24

int32 UAVXPacketi32(uint8 p) {
	int32 temp;

	temp = (int32) (UAVXPacket[p + 3] << 24);
	temp |= ((int32) UAVXPacket[p + 2] << 16);
	temp |= ((int32) UAVXPacket[p + 1] << 8);
	temp |= (int32) UAVXPacket[p];
	return temp;
} // UAVXPacketi32


//______________________________________________________________________________________________

// Tx UAVX Packets

#define NAV_STATS_INTERLEAVE	10
int8 StatsNavAlternate = 0;

void SendPacketHeader(uint8 s) {
	TxChar(s, 0xff); // synchronisation to "jolt" USART
	TxChar(s, ASCII_SOH);
	TxCheckSum[s] = 0;
} // SendPacketHeader

void SendPacketTrailer(uint8 s) {

	TxESCu8(s, TxCheckSum[s]);
	TxChar(s, ASCII_EOT);

	TxChar(s, ASCII_CR);
	TxChar(s, ASCII_LF);
} // SendPacketTrailer

void SendAckPacket(uint8 s, uint8 Tag, uint8 Reason) {

	SendPacketHeader(s);

	TxESCu8(s, UAVXAckPacketTag);
	TxESCu8(s, 2);

	TxESCu8(s, Tag); // 2
	TxESCu8(s, Reason); // 3

	SendPacketTrailer(s);
} // SendAckPacket

void ShowDrives(uint8 s) {
	real32 AvSum;
	real32 PWSamplesR;
	uint8 b;

	PWSamplesR = 1.0 / PWSamples;

	AvSum = 0.0f;
	for (b = 0; b < NoOfDrives; b++)
		AvSum += PWSum[b];
	AvSum *= NoOfDrivesR;

	for (b = 0; b < NoOfDrives; b++)
		PWDiagnostic[b] = Limit1((PWSum[b] - AvSum) * PWSamplesR * 100, 100);

	TxESCi8(s, CurrMaxPWMOutputs);
	for (b = 0; b < CurrMaxPWMOutputs; b++) // motor/servo channels
		TxESCi16(s, PW[b] * 1000);
	for (b = 0; b < CurrMaxPWMOutputs; b++)
		TxESCi8(s, PWDiagnostic[b]);

} // ShowDrives

void ShowAttitude(uint8 s) {

	TxESCi16(s, A[Roll].Desired * 1000.0f);
	TxESCi16(s, A[Pitch].Desired * 1000.0f);
	TxESCi16(s, A[Yaw].Desired * 1000.0f);

	TxESCi16(s, Rate[Roll] * 1000.0f);
	TxESCi16(s, Rate[Pitch] * 1000.0f);
	TxESCi16(s, Rate[Yaw] * 1000.0f);

	TxESCi16(s, A[Roll].Angle * 1000.0f);
	TxESCi16(s, A[Pitch].Angle * 1000.0f);

	TxESCi16(s, A[Yaw].Angle * 1000.0f);

	TxESCi16(s, Acc[BF] * 1000.0f * GRAVITY_MPS_S_R);
	TxESCi16(s, Acc[LR] * 1000.0f * GRAVITY_MPS_S_R);
	TxESCi16(s, Acc[UD] * 1000.0f * GRAVITY_MPS_S_R); //

} // ShowAttitude


void SendFlightPacket(uint8 s) {
	uint8 b;

	SendPacketHeader(s);

	TxESCu8(s, UAVXFlightPacketTag);
	TxESCu8(s, 55 + CurrMaxPWMOutputs * 3 + 2 + TELEMETRY_FLAG_BYTES + 4 + 8
			+ 2 + 2);
	for (b = 0; b < TELEMETRY_FLAG_BYTES; b++)
		TxESCu8(s, F.AllFlags[b]);

	TxESCu8(s, State);

	TxESCi16(s, BatteryVolts * 100.0f);
	TxESCi16(s, BatteryCurrent * 100.0f);

	TxESCi16(s, BatteryChargeUsedmAH);

	TxESCi16(s, NV.Stats[RCGlitchesS]);
	TxESCi16(s, NetThrottle * 1000.0f); // was DesiredThrottle

	ShowAttitude(s);

	TxESCi16(s, ROC * 100.0f); // cm/S
	TxESCi24(s, Altitude * 100.0f); // cm

	TxESCi16(s, CruiseThrottle * 1000.0f);

	TxESCi16(s, RangefinderAltitude * 100.0f); // cm
	TxESCi24(s, DesiredAltitude * 100.0f);

	TxESCi16(s, Heading * 1000.0f);
	TxESCi16(s, DesiredHeading * 1000.0f);

	TxESCi16(s, TiltThrFFComp * 1000.0f);
	TxESCi16(s, BattThrFFComp * 1000.0f);
	TxESCi16(s, AltComp * 1000.0f);

	TxESCi8(s, AccConfidence * 100.0f);
	TxESCi16(s, BaroTemperature * 100.0f);
	TxESCi24(s, BaroPressure * 10.0f);

	TxESCi24(s, BaroAltitude * 100.0f);

	TxESCi8(s, AccZ * 100.0f);

	TxESCi16(s, Make2Pi(MagHeading) * 1000.0f);

	ShowDrives(s);

	// 	TxESCi16(s, MPU6XXXTemperature * 10.0f); // 0.1C
	TxESCi24(s, mSClock() - mS[StartTime]);

	SendPacketTrailer(s);
} // SendFlightPacket

void SendControlPacket(uint8 s) {

	SendPacketHeader(s);

	TxESCu8(s, UAVXControlPacketTag);
	TxESCu8(s, 31 + CurrMaxPWMOutputs * 3);

	TxESCi16(s, NetThrottle * 1000.0f);

	ShowAttitude(s);

	TxESCu8(s, UAVXAirframe | 0x80);

	ShowDrives(s);

	//TxESCi16(s, MPU6XXXTemperature * 10.0f); // 0.1C
	TxESCi24(s, mSClock() - mS[StartTime]);

	SendPacketTrailer(s);

} // SendControlPacket

void SendNavPacket(uint8 s) {

	SendPacketHeader(s);

	TxESCu8(s, UAVXNavPacketTag);
	TxESCu8(s, 51);

	TxESCu8(s, NavState);
	TxESCu8(s, FailState);
	TxESCu8(s, GPS.noofsats);
	TxESCu8(s, GPS.fix);

	TxESCu8(s, CurrWPNo);

	TxESCi16(s, GPS.hAcc * 100.0f);

	TxESCi16(s, Nav.WPBearing * 1000.0f);
	TxESCi16(s, Nav.CrossTrackE * 10.0f);

	TxESCi16(s, GPS.gspeed * 10.0f); // dM/S
	TxESCi16(s, Make2Pi(GPS.heading) * 1000.0f); // milliRadians

	TxESCi24(s, GPS.altitude * 100.0f);
	TxESCi32(s, GPS.Raw[NorthC]); // 1.0-7 degrees
	TxESCi32(s, GPS.Raw[EastC]);

	TxESCi32(s, Nav.PosE[NorthC] * 10.0f); // dM
	TxESCi32(s, Nav.PosE[EastC] * 10.0f);

	TxESCi24(s, mS[NavStateTimeout] - mSClock()); // mS

	TxESCi16(s, MPU6XXXTemperature * 10.0f); // 0.1C
	TxESCi32(s, GPS.missionTime);

	TxESCi16(s, Nav.Sensitivity * 1000.0f);

	TxESCi16(s, A[Pitch].NavCorr * 1000.0f);
	TxESCi16(s, A[Roll].NavCorr * 1000.0f);

	TxESCi16(s, RadiansToDegrees(HeadingE));

	SendPacketTrailer(s);

} // SendNavPacket

void SendSoaringPacket(uint8 s) {
	/*
	 if (F.Glide) {
	 SendPacketHeader(s);

	 TxESCu8(s, UAVXSoaringPacketTag);
	 TxESCu8(s, ??);

	 uint32 SoaringTune.mS;
	 real32 SoaringTune.vario;
	 real32 SoaringTune.thermalstrength;
	 real32 SoaringTune.dx;
	 real32 SoaringTune.dy;
	 real32 SoaringTune.x0;
	 real32 SoaringTune.x1;
	 real32 SoaringTune.x2;
	 real32 SoaringTune.x3;
	 uint32 SoaringTune.lat;
	 uint32 SoaringTune.lon;
	 real32 SoaringTune.alt;
	 real32 SoaringTune.dx_w;
	 real32 SoaringTune.dy_w;

	 SendPacketTrailer(s);
	 }
	 */
} // SendSoaringPacket

void SendFusionPacket(uint8 s) {

	SendPacketHeader(s);

	TxESCu8(s, UAVXFusionPacketTag);
	TxESCu8(s, 15);

	TxESCi16(s, AccZ * 1000.0f);

	TxESCi24(s, FAltitude * 100.0f);
	TxESCi16(s, FROC * 100.0f);
	TxESCi16(s, NV.AccCal.DynamicAccBias[Z] * 1000.0f);

	TxESCi16(s, 0);
	TxESCi16(s, 0);
	TxESCi16(s, 0);

	SendPacketTrailer(s);

} // SendFusionPacket

void SendPIDPacket(uint8 s) {

	SendPacketHeader(s);

	TxESCu8(s, UAVXPIDPacketTag);
	TxESCu8(s, 15);

	TxESCi16(s, AccZ * 1000.0f);

	TxESCi24(s, FAltitude * 100.0f);
	TxESCi16(s, FROC * 100.0f);
	TxESCi16(s, NV.AccCal.DynamicAccBias[Z] * 1000.0f);

	TxESCi16(s, 0);
	TxESCi16(s, 0);
	TxESCi16(s, 0);

	SendPacketTrailer(s);

} // SendPIDPacket

void SendCalibrationPacket(uint8 s) {
	SendPacketHeader(s);
	uint8 a;

	TxESCu8(s, UAVXCalibrationPacketTag);
	TxESCu8(s, 2 + 36);

	TxESCi16(s, NV.GyroCal.TRef * 10.0f);

	for (a = 0; a <= Z; a++) {
		TxESCi16(s, RadiansToDegrees(NV.GyroCal.M[a])
				* GyroScale[CurrAttSensorType] * 1000.0f);
		TxESCi16(s, RadiansToDegrees(NV.GyroCal.C[a])
				* GyroScale[CurrAttSensorType] * 1000.0f);

		TxESCi16(s, NV.AccCal.M[a] * 1000.0f * AccScale * GRAVITY_MPS_S_R);
		TxESCi16(s, NV.AccCal.C[a] * 1000.0f * AccScale * GRAVITY_MPS_S_R);

		TxESCi16(s, NV.MagCal.Scale[a] * 1000.0f);
		TxESCi16(s, NV.MagCal.Bias[a] * 1000.0f);
	}

	SendPacketTrailer(s);
} // SendCalibrationPacket


void SendParamsPacket(uint8 s, uint8 GUIPS) {
	int32 p;

	if ((State == Preflight) || (State == Ready)) {

		if (GUIPS == 255) {
			UseDefaultParameters();
			SendAckPacket(s, UAVXParamPacketTag, true);
		} else {
			if (GUIPS != 254) // not previous PS
				NV.CurrPS = GUIPS;

			if ((NV.CurrPS >= 0) && (NV.CurrPS <= NO_OF_PARAM_SETS)) {

				SendPacketHeader(s);

				TxESCu8(s, UAVXParamPacketTag);
				TxESCu8(s, 1 + MAX_PARAMETERS);

				TxESCu8(s, NV.CurrPS);
				for (p = 0; p < MAX_PARAMETERS; p++)
					TxESCi8(s, NV.P[NV.CurrPS][p]);

				SendPacketTrailer(s);

			} else
				SendAckPacket(s, UAVXParamPacketTag, false);
		}
	}

} // SendParamsPacket

void SendRCChannelsPacket(uint8 s) {
	uint8 c;

	SendPacketHeader(s);

	TxESCu8(s, UAVXRCChannelsPacketTag);
	TxESCu8(s, 12 * 2 + 2 + 1);
	TxESCi16(s, RCFrameIntervaluS);
	TxESCu8(s, DiscoveredRCChannels);
	for (c = 0; c < 12; c++)
		TxESCi16(s, RC[c] * 1000.0f + 1000.0f);

	SendPacketTrailer(s);

} // SendRCChannelsPacket

void SendStatsPacket(uint8 s) {
	uint8 i, len;

	len = strlen(Revision);

	SendPacketHeader(s);

	TxESCu8(s, UAVXStatsPacketTag);
	TxESCu8(s, MAX_STATS * 2 + 1 + 1 + len);

	for (i = 0; i < MAX_STATS; i++)
		TxESCi16(s, NV.Stats[i]);

	TxESCu8(s, UAVXAirframe | 0x80);

	TxESCu8(s, len);
	for (i = 0; i < len; i++)
		TxESCu8(s, Revision[i]);

	SendPacketTrailer(s);

} // SendStatsPacket

void SendBBPacket(uint8 s, int32 seqNo, uint8 l, int8 * B) {
	uint8 i;

	SendPacketHeader(s);

	TxESCu8(s, UAVXBBPacketTag);
	TxESCu8(s, l + 4 + 2);
	TxESCi32(s, seqNo);
	TxESCi16(s, l);
	for (i = 0; i < l; i++)
		TxESCu8(s, B[i]);

	SendPacketTrailer(s);

} // SendBBPacket

void SendDFTPacket(uint8 s) {
#if defined(INC_DFT)
	uint8 i;

	SendPacketHeader(s);

	TxESCu8(s, UAVXDFTPacketTag);
	TxESCu8(s, 2 + 8 * 2);

	TxESCi16(s, 1000000 / CurrPIDCycleuS);
	for (i = 0; i < 8; i++)
		TxESCi16(s, DFT[i] * AccScale * GRAVITY_MPS_S_R * 1000.0);

	SendPacketTrailer(s);
#endif
} // SendDFTPacket

void SendMinPacket(uint8 s) {
	uint8 b;

	SendPacketHeader(s);

	TxESCu8(s, UAVXMinPacketTag);
	TxESCu8(s, 33 + TELEMETRY_FLAG_BYTES);
	for (b = 0; b < TELEMETRY_FLAG_BYTES; b++)
		TxESCu8(s, F.AllFlags[b]);

	TxESCu8(s, State);
	TxESCu8(s, NavState);
	TxESCu8(s, FailState);

	TxESCi16(s, BatteryVolts * 100.0f);
	TxESCi16(s, BatteryCurrent * 100.0f);
	TxESCi16(s, BatteryChargeUsedmAH);

	TxESCi16(s, A[Roll].Angle * 1000.0f);
	TxESCi16(s, A[Pitch].Angle * 1000.0f);

	TxESCi24(s, Altitude * 100.0f);
	TxESCi16(s, ROC * 100.0f);

	TxESCi16(s, Make2Pi(Heading) * 1000.0f);

	TxESCi32(s, GPS.Raw[NorthC]);
	TxESCi32(s, GPS.Raw[EastC]);

	TxESCu8(s, UAVXAirframe | 0x80);
	TxESCu8(s, 0); // Orientation);

	TxESCi24(s, mSClock() - mS[StartTime]);

	SendPacketTrailer(s);

} // SendMinPacket

void SendMinimOSDPacket(uint8 s) {

	SendPacketHeader(s);

	TxESCu8(s, UAVXMinimOSDPacketTag);
	TxESCu8(s, 41);

	TxESCi16(s, BatteryVolts * 1000.0f); //2
	TxESCi16(s, BatteryCurrent * 10.0f); // ??
	TxESCi16(s, (BatteryChargeUsedmAH * 100.0) / BatteryCapacitymAH);

	TxESCi16(s, RadiansToDegrees(A[Roll].Angle));
	TxESCi16(s, RadiansToDegrees(A[Pitch].Angle));

	TxESCi24(s, Altitude);
	TxESCi16(s, ROC * 10.0);

	TxESCi16(s, GPS.gspeed * 10.0f);

	TxESCi16(s, RadiansToDegrees(Make2Pi(Heading)));

	TxESCi32(s, GPS.Raw[NorthC]);
	TxESCi32(s, GPS.Raw[EastC]);

	TxESCu8(s, GPS.noofsats);
	TxESCu8(s, GPS.fix);

	TxESCu8(s, CurrWPNo);
	TxESCi16(s, RadiansToDegrees(Nav.WPBearing));
	TxESCi16(s, Nav.WPDistance);
	TxESCi16(s, Nav.CrossTrackE);

	TxESCu8(s, Armed());
	TxESCi16(s, NetThrottle * 100.0f);

	TxESCu8(s, NavState);
	TxESCu8(s, UAVXAirframe | 0x80);

	SendPacketTrailer(s);

} // SendMinimOSD


void SendOriginPacket(uint8 s) {
	MissionStruct * M;

	M = &NV.Mission;

	SendPacketHeader(s);

	TxESCu8(s, UAVXOriginPacketTag);
	TxESCu8(s, 15);

	TxESCu8(s, M->NoOfWayPoints); // 0
	TxESCu8(s, M->ProximityAltitude); // 1
	TxESCu8(s, M->ProximityRadius); // 2
	TxESCi16(s, M->OriginAltitude); // 3

	TxESCi32(s, M->OriginLatitude); // 5
	TxESCi32(s, M->OriginLongitude); // 9

	TxESCi16(s, M->RTHAltHold); // 13 + 1

	SendPacketTrailer(s);
} // SendOriginPacket

void SendGuidancePacket(uint8 s) {

	SendPacketHeader(s);

	TxESCu8(s, UAVXGuidancePacketTag);
	TxESCu8(s, 6);

	TxESCi16(s, Nav.Distance);
	TxESCi16(s, RadiansToDegrees(Nav.Bearing));

	TxESCi8(s, RadiansToDegrees(Nav.Elevation));
	TxESCi8(s, RadiansToDegrees(Nav.Hint));

	SendPacketTrailer(s);

} // SendGuidance

void SendWPPacket(uint8 s, uint8 wp) {
	MissionStruct * M;

	M = &NV.Mission;

	SendPacketHeader(s);

	TxESCu8(s, UAVXWPPacketTag);
	TxESCu8(s, 22);

	TxESCu8(s, wp);
	TxESCi32(s, M->WP[wp].LatitudeRaw); // 1e7/degree
	TxESCi32(s, M->WP[wp].LongitudeRaw);
	TxESCi16(s, M->WP[wp].Altitude);
	TxESCi16(s, M->WP[wp].VelocitydMpS); // dM/S
	TxESCi16(s, M->WP[wp].Loiter); // S
	TxESCi16(s, M->WP[wp].OrbitRadius);
	TxESCi16(s, M->WP[wp].OrbitAltitude); // M relative to Origin
	TxESCi16(s, M->WP[wp].OrbitVelocitydMpS); // dM/S
	TxESCu8(s, M->WP[wp].Action);

	SendPacketTrailer(s);
} // SendMissionWPPacket


void SendMission(uint8 s) {
	uint8 wp;

	SendNavPacket(s);
	for (wp = 1; wp <= NV.Mission.NoOfWayPoints; wp++)
		SendWPPacket(s, wp);

	SendOriginPacket(s);
} // SendMission

//______________________________________________________________________________________________

//UAVX Telemetry


void SetTelemetryBaudRate(uint8 s, uint32 b) {
	static uint32 CurrTelemetryBaudRate = 42;

	if (b != CurrTelemetryBaudRate) {
		serialBaudRate(s, b);
		CurrTelemetryBaudRate = b;
	}

} // SetTelemetryBaudRate

//______________________________________________________________________________________________

// Rx UAVX Packets


void ProcessParamsPacket(uint8 s) {
	uint8 p, NewCurrPS;

	if ((State == Preflight) || (State == Ready)) { // not inflight

		NewCurrPS = UAVXPacket[2];
		if ((NewCurrPS >= 0) && (NewCurrPS < NO_OF_PARAM_SETS)) {

			NV.CurrPS = NewCurrPS;

			for (p = 0; p < MAX_PARAMETERS; p++)
				SetP(p, UAVXPacketi8(p + 3));

			UpdateNV();

			F.ParametersChanged = true;
			UpdateParameters();

			SendAckPacket(s, UAVXParamPacketTag, true);
		} else
			SendAckPacket(s, UAVXParamPacketTag, false);
	} else
		SendAckPacket(s, UAVXParamPacketTag, false);

} // ProcessParamsPacket

void ProcessWPPacket(uint8 s) {
	uint8 wp;
	WPStructNV * W;

	wp = UAVXPacket[2];
	W = &NV.NewMission.WP[wp];

	W->LatitudeRaw = UAVXPacketi32(3);
	W->LongitudeRaw = UAVXPacketi32(7);
	W->Altitude = UAVXPacketi16(11);
	W->VelocitydMpS = UAVXPacketi16(13);
	W->Loiter = UAVXPacketi16(15);

	W->OrbitRadius = UAVXPacketi16(17);
	W->OrbitAltitude = UAVXPacketi16(19);
	W->OrbitVelocitydMpS = UAVXPacketi16(21);
	W->Action = UAVXPacket[23];

} // ReceiveWPPacket

void ProcessOriginPacket(uint8 s) {
	MissionStruct * M;

	M = &NV.NewMission;

	M->NoOfWayPoints = UAVXPacket[2];

	M->ProximityAltitude = UAVXPacketi16(3);
	M->ProximityRadius = UAVXPacketi16(4);

	/* NOT updated by UAVXNav set ONLY by GPS at launch
	 M->OriginAltitude = UAVXPacketi16(5);
	 M->OriginLatitude = UAVXPacketi32(7);
	 M->OriginLongitude = UAVXPacketi32(11);
	 */

	M->RTHAltHold = UAVXPacketi16(15);

	DoMissionUpdate();

} // ProcessOriginPacket

void ProcessEscProgPacket(void) {
	esc4wayInit();

}

void InitPollRxPacket(void) {

	RxPacketByteCount = 0;
	RxCheckSum = 0;

	RxPacketTag = UnknownPacketTag;

	RxPacketLength = 2; // set as minimum
	PacketRxState = WaitRxSentinel;
} // InitRxPollPacket

void AddToRxPacketBuffer(uint8 ch) {
	boolean RxPacketError;

	UAVXPacket[RxPacketByteCount++] = ch;
	if (RxPacketByteCount == 1) {
		RxPacketTag = ch;
		PacketRxState = WaitRxBody;
	} else if (RxPacketByteCount == 2) {
		RxPacketLength = ch;
		PacketRxState = WaitRxBody;
	} else if (RxPacketByteCount >= (RxPacketLength + 3)) {
		RxPacketError = CheckSumError = false; //TODO: zzz !((RxCheckSum == 0) || (RxCheckSum
		//== ASCII_ESC));

		if (CheckSumError)
			RxCheckSumErrors++;

		if (!RxPacketError) {
			PacketReceived = true;
			ReceivedPacketTag = RxPacketTag;
		}
		PacketRxState = WaitRxSentinel;
		//   InitPollPacket();
	} else
		PacketRxState = WaitRxBody;
} // AddToRxPacketBuffer

void ParseRxPacket(uint8 ch) {

	RxCheckSum ^= ch;
	switch (PacketRxState) {
	case WaitRxSentinel:
		if (ch == ASCII_SOH) {
			InitPollRxPacket();
			CheckSumError = false;
			PacketRxState = WaitRxBody;
		}
		break;
	case WaitRxBody:
		if (ch == ASCII_ESC)
			PacketRxState = WaitRxESC;
		else if (ch == ASCII_SOH) // unexpected start of packet
		{
			RxLengthErrors++;
			InitPollRxPacket();
			PacketRxState = WaitRxBody;
		} else if (ch == ASCII_EOT) // unexpected end of packet
		{
			RxLengthErrors++;
			PacketRxState = WaitRxSentinel;
		} else
			AddToRxPacketBuffer(ch);
		break;
	case WaitRxESC:
		AddToRxPacketBuffer(ch);
		break;
	default:
		PacketRxState = WaitRxSentinel;
		break;
	}
} // ParseRxPacket

void ProcessRxPacket(uint8 s) {

	PacketReceived = false;
	PacketsReceived[RxPacketTag]++;

	LEDOn(LEDBlueSel);

	switch (RxPacketTag) {
	case UAVXRequestPacketTag:
		switch (UAVXPacket[2]) {
		case UAVXMiscPacketTag:
			switch (UAVXPacket[3]) {
			case miscCalIMU:
				CalibrateAccAndGyro(s);
				SendCalibrationPacket(s);
				break;
			case miscCalMag:
				CalibrateHMC5XXX(s);
				SendCalibrationPacket(s);
				break;
			case miscLB:
				if ((CurrComboPort1Config != CPPM_GPS_M7to10)
						&& (CurrComboPort1Config != ParallelPPM))
					RxLoopbackEnabled = !RxLoopbackEnabled;
				SendAckPacket(s, miscLB, RxLoopbackEnabled);
				break;
			case miscBBDump:
				BBReplaySpeed = UAVXPacket[4];
				DumpBlackBox(s);
				break;
			case miscProgEsc:
				if (IsMulticopter)
					DoBLHeliSuite(s);
				break;
			default:
				break;
			} // switch
			break;
		case UAVXParamPacketTag:
			SendParamsPacket(s, UAVXPacket[3]);
			break;
		case UAVXMissionPacketTag:
			SendMission(s);
			break;
		case UAVXOriginPacketTag:
			SendOriginPacket(s);
			break;
		case UAVXWPPacketTag:
			SendWPPacket(s, UAVXPacket[3]);
			break;
		case UAVXMinPacketTag:
			SendMinPacket(s);
			break;
		case UAVXStatsPacketTag:
			SendStatsPacket(s);
			break;
		case UAVXFlightPacketTag:
			SendFlightPacket(s);
			break;
		case UAVXControlPacketTag:
			SendControlPacket(s);
			break;
		case UAVXNavPacketTag:
			SendNavPacket(s);
			break;
		default:
			SendAckPacket(s, RxPacketTag, 255);
			break;
		} // switch
		break;
	case UAVXParamPacketTag:
		ProcessParamsPacket(s);
		break;
	case UAVXOriginPacketTag:
		ProcessOriginPacket(s);
		break;
	case UAVXWPPacketTag:
		ProcessWPPacket(s);
		break;
	default:
		break;
	} // switch

	LEDOff(LEDBlueSel);

} // ProcessRxPacket

void UAVXPollRx(uint8 s) {
	uint8 ch;

	if (F.UsingUplink) {
		if (serialAvailable(s)) {
			ch = RxChar(s);
			ParseRxPacket(ch);
		}

		if (PacketReceived)
			ProcessRxPacket(s);
	}

} // UAVXPollRx

void UseUAVXTelemetry(uint8 s) {
	static boolean SendFlight = true;

	uint32 NowmS = mSClock();
	if (NowmS >= mS[TelemetryUpdate]) {
		mSTimer(NowmS, TelemetryUpdate, UAVX_TEL_INTERVAL_MS);
		if (SendFlight) {
			//if (!Armed())
			SendFlightPacket(s); // 78
			SendFusionPacket(s);
			SendGuidancePacket(s); // 2+7
			SendRCChannelsPacket(s); // 27 -> 105
		} else {
			SendNavPacket(s); // 2+54+4 = 60
			SendDFTPacket(s); // 24
			SendStatsPacket(s); // ~80 -> 104
			if (State == Warmup)
				SendCalibrationPacket(s);
		}
		SendFlight = !SendFlight;
	}
} // UseUAVXTelemetry

void CheckTelemetry(uint8 s) {
	// KLUDGE - MAVLink is only active when armed as we need UAVXGUI
	// which uses UAVXTelemetry for reconfiguration.

	uint32 NowmS;

	if (!(F.UsingMAVLink && Armed()))
		UAVXPollRx(s);

	BlackBoxEnabled = true;

	NowmS = mSClock();
	if (!Armed()) {
		SetTelemetryBaudRate(s, 115200);
		UseUAVXTelemetry(s);
	} else
		switch (CurrTelType) {
		case UAVXTelemetry:
			SetTelemetryBaudRate(s, 115200);
			UseUAVXTelemetry(s);
			break;
		case UAVXMinTelemetry:
			SetTelemetryBaudRate(s, 115200);
			if (NowmS >= mS[TelemetryUpdate]) {
				mSTimer(NowmS, TelemetryUpdate, UAVX_MIN_TEL_INTERVAL_MS);
				SendMinPacket(s);
			}
			break;
		case UAVXMinimOSDTelemetry:
			SetTelemetryBaudRate(s, 57600);
			if (NowmS >= mS[TelemetryUpdate]) {
				mSTimer(NowmS, TelemetryUpdate, UAVX_MINIMOSD_TEL_INTERVAL_MS);
				SendMinimOSDPacket(s);
				SendRCChannelsPacket(s); // TODO: alternate?
			}
			break;
		case MAVLinkMinTelemetry:
			SetTelemetryBaudRate(s, 57600);
			mavlinkUpdate(s);
			break;
		case MAVLinkTelemetry:
			SetTelemetryBaudRate(s, 115200);
			mavlinkUpdate(s);
			break;
		case FrSkyTelemetry:
			SetTelemetryBaudRate(s, 9600);
			if (NowmS >= mS[TelemetryUpdate]) {
				mSTimer(NowmS, TelemetryUpdate, FRSKY_TEL_INTERVAL_MS);
				SendFrSkyTelemetry(s);
			}
			break;
		default:
			break;
		} // switch

	BlackBoxEnabled = false;

	UpdateBlackBox();

}
// CheckTelemetry


