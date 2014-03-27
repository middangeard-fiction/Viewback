#include "viewback_client.h"

#include <sstream>
#include <sys/timeb.h>
#include <stdarg.h>

#include "../server/viewback_shared.h"

#include "viewback_servers.h"
#include "viewback_data.h"

using namespace std;

static CViewbackClient* VB = NULL;

void vb_debug_printf(const char* format, ...)
{
	if (!VB->GetDebugOutputCallback())
		return;

	char buf[1024];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);

	VB->GetDebugOutputCallback()(buf);
}

bool CViewbackClient::Initialize(RegistrationUpdateCallback pfnRegistration, ConsoleOutputCallback pfnConsoleOutput, DebugOutputCallback pfnDebugOutput)
{
	VB = this;

	m_flNextDataClear = 10;
	m_flDataClearTime = 0;

	m_pfnRegistrationUpdate = pfnRegistration;
	m_pfnConsoleOutput = pfnConsoleOutput;
	m_pfnDebugOutput = pfnDebugOutput;

	m_bDisconnected = false;

	return CViewbackServersThread::Run();
}

void CViewbackClient::Update()
{
	if (CViewbackDataThread::IsConnected())
	{
		size_t iStartPacket = 0;
		vector<Packet> aPackets = CViewbackDataThread::GetData();

		// Look for a data registration packet.
		for (size_t i = 0; i < aPackets.size(); i++)
		{
			if (aPackets[i].data_channels_size())
			{
				static VBVector3 aclrColors[] = {
					VBVector3(1, 0, 0),
					VBVector3(0, 1, 0),
					VBVector3(0, 0, 1),
					VBVector3(0, 1, 1),
					VBVector3(1, 0, 1),
					VBVector3(1, 1, 0),
				};
				int iColorsSize = sizeof(aclrColors) / sizeof(aclrColors[0]);

				// Disregard any data which came in before the registration packet, it may be from another server or old connection.
				m_aData.clear();
				m_aMeta.clear();
				m_aUnhandledMessages.clear();
				iStartPacket = i + 1;

				m_aDataChannels.resize(aPackets[i].data_channels_size());
				m_aData.resize(aPackets[i].data_channels_size());
				m_aMeta.resize(aPackets[i].data_channels_size());

				for (int j = 0; j < aPackets[i].data_channels_size(); j++)
				{
					auto& oChannelProtobuf = aPackets[i].data_channels(j);

					VBAssert(oChannelProtobuf.has_handle());
					VBAssert(oChannelProtobuf.has_name());
					VBAssert(oChannelProtobuf.has_type());
					VBAssert(oChannelProtobuf.handle() == j);

					auto& oChannel = m_aDataChannels[oChannelProtobuf.handle()];
					oChannel.m_iHandle = oChannelProtobuf.handle();
					oChannel.m_sFieldName = oChannelProtobuf.name();
					oChannel.m_eDataType = oChannelProtobuf.type();

					if (oChannelProtobuf.has_range_min())
						oChannel.m_flMin = oChannelProtobuf.range_min();

					if (oChannelProtobuf.has_range_max())
						oChannel.m_flMax = oChannelProtobuf.range_max();

					m_aMeta[oChannelProtobuf.handle()].m_clrColor = aclrColors[j % iColorsSize];
				}

				VBPrintf("Installed %d channels.\n", aPackets[i].data_channels_size());

				for (int j = 0; j < aPackets[i].data_groups_size(); j++)
				{
					auto& oGroupProtobuf = aPackets[i].data_groups(j);

					VBAssert(oGroupProtobuf.has_name());

					m_aDataGroups.push_back(CViewbackDataGroup());
					auto& oGroup = m_aDataGroups.back();
					oGroup.m_sName = oGroupProtobuf.name();
					for (int k = 0; k < oGroupProtobuf.channels_size(); k++)
						oGroup.m_iChannels.push_back(oGroupProtobuf.channels(k));
				}

				VBPrintf("Installed %d groups.\n", aPackets[i].data_groups_size());

				for (int j = 0; j < aPackets[i].data_labels_size(); j++)
				{
					auto& oLabelProtobuf = aPackets[i].data_labels(j);

					VBAssert(oLabelProtobuf.has_label());
					VBAssert(oLabelProtobuf.has_channel());
					VBAssert(oLabelProtobuf.has_value());

					auto& oChannel = m_aDataChannels[oLabelProtobuf.channel()];
					oChannel.m_asLabels[oLabelProtobuf.value()] = oLabelProtobuf.label();
				}

				VBPrintf("Installed %d labels.\n", aPackets[i].data_labels_size());

				if (m_pfnRegistrationUpdate)
					m_pfnRegistrationUpdate();
			}
		}

		if (!m_aDataChannels.size())
		{
			// We somehow don't have any data registrations yet, so stash these messages for later.
			// It might be possible if the server sends some messages between when the client connects and when it requests registrations.
			for (size_t i = 0; i < aPackets.size(); i++)
				m_aUnhandledMessages.push_back(aPackets[i]);

			return;
		}

		// If we've been saving any messages, stick them onto the beginning here.
		if (m_aUnhandledMessages.size())
			aPackets.insert(aPackets.begin(), m_aUnhandledMessages.begin(), m_aUnhandledMessages.end());

		m_aUnhandledMessages.clear();

		for (size_t i = iStartPacket; i < aPackets.size(); i++)
		{
			if (aPackets[i].has_data())
				StashData(&aPackets[i].data());

			if (aPackets[i].has_console_output() && m_pfnConsoleOutput)
				m_pfnConsoleOutput(aPackets[i].console_output().c_str());

			if (aPackets[i].has_status())
				m_sStatus = aPackets[i].status();
		}

		while (m_sOutgoingCommands.size())
		{
			if (CViewbackDataThread::SendConsoleCommand(m_sOutgoingCommands.front()))
				// Message was received, we can remove this command from the list.
				m_sOutgoingCommands.pop_front();
			else
				// There's already a command waiting to be sent to the data thread, hold on for now.
				break;
		}

		// Clear out old data. First let's find the newest timestamp of any data.
		double flNewest = GetLatestDataTime();
		if (flNewest > m_flNextDataClear)
		{
			for (size_t i = 0; i < m_aData.size(); i++)
			{
				if (m_aDataChannels[i].m_eDataType == VB_DATATYPE_VECTOR)
				{
					while (m_aData[i].m_aVectorData.size() && m_aData[i].m_aVectorData.front().time < flNewest - m_aMeta[i].m_flDisplayDuration - 10)
						m_aData[i].m_aVectorData.pop_front();
				}
				else if (m_aDataChannels[i].m_eDataType == VB_DATATYPE_FLOAT)
				{
					while (m_aData[i].m_aFloatData.size() && m_aData[i].m_aFloatData.front().time < m_flDataClearTime)
						m_aData[i].m_aFloatData.pop_front();
				}
				else if (m_aDataChannels[i].m_eDataType == VB_DATATYPE_INT)
				{
					while (m_aData[i].m_aIntData.size() && m_aData[i].m_aIntData.front().time < m_flDataClearTime)
						m_aData[i].m_aIntData.pop_front();
				}
				else
					VBAssert(false);
			}

			m_flNextDataClear = flNewest + 10;
		}
	}
	else
	{
		for (size_t i = 0; i < m_aData.size(); i++)
		{
			auto& oData = m_aData[i];

			oData.m_aFloatData.clear();
			oData.m_aIntData.clear();
			oData.m_aVectorData.clear();
		}

		unsigned long best_server = CViewbackServersThread::GetServer();

		if (!m_bDisconnected && best_server)
		{
			struct in_addr inaddr;
			inaddr.s_addr = htonl(best_server);
			VBPrintf("Connecting to server at %s ...\n", inet_ntoa(inaddr));

			bool bResult = CViewbackDataThread::Connect(best_server);

			if (bResult)
				VBPrintf("Success.\n");
			else
				VBPrintf("Failed.\n");

			if (bResult)
			{
				struct timeb now;
				now.time = 0;
				now.millitm = 0;

				ftime(&now);

				m_iServerConnectionTimeS = now.time;
				m_iServerConnectionTimeMS = now.millitm;
				m_flDataClearTime = 0;
			}
		}
	}
}

bool CViewbackClient::HasConnection()
{
	return CViewbackDataThread::IsConnected();
}

void CViewbackClient::Connect(const char* pszIP, int iPort)
{
	CViewbackDataThread::Disconnect();
	m_bDisconnected = false;
	CViewbackDataThread::Connect(ntohl(inet_addr(pszIP)), iPort);
}

void CViewbackClient::FindServer()
{
	m_bDisconnected = false;
}

void CViewbackClient::Disconnect()
{
	m_bDisconnected = true;
	CViewbackDataThread::Disconnect();
}

void CViewbackClient::SendConsoleCommand(const string& sCommand)
{
	// This list is pumped into the data thread during the Update().
	m_sOutgoingCommands.push_back(sCommand);
}

vb_data_type_t CViewbackClient::TypeForHandle(size_t iHandle)
{
	return m_aDataChannels[iHandle].m_eDataType;
}

bool CViewbackClient::HasLabel(size_t iHandle, int iValue)
{
	auto it = m_aDataChannels[iHandle].m_asLabels.find(iValue);
	if (it == m_aDataChannels[iHandle].m_asLabels.end())
		return false;
	else
		return true;
}

string CViewbackClient::GetLabelForValue(size_t iHandle, int iValue)
{
	auto it = m_aDataChannels[iHandle].m_asLabels.find(iValue);
	if (it == m_aDataChannels[iHandle].m_asLabels.end())
		return static_cast<ostringstream*>(&(ostringstream() << iValue))->str();
	else
		return it->second;
}

void CViewbackClient::StashData(const Data* pData)
{
	switch (TypeForHandle(pData->handle()))
	{
	case VB_DATATYPE_NONE:
	default:
		VBAssert(false);
		break;

	case VB_DATATYPE_INT:
		m_aData[pData->handle()].m_aIntData.push_back(CViewbackDataList::DataPair<int>(pData->time(), pData->data_int()));
		break;

	case VB_DATATYPE_FLOAT:
		m_aData[pData->handle()].m_aFloatData.push_back(CViewbackDataList::DataPair<float>(pData->time(), pData->data_float()));
		break;

	case VB_DATATYPE_VECTOR:
		m_aData[pData->handle()].m_aVectorData.push_back(CViewbackDataList::DataPair<VBVector3>(pData->time(), VBVector3(pData->data_float_x(), pData->data_float_y(), pData->data_float_z())));
		break;
	}

	if (pData->time() > m_flLatestDataTime)
	{
		m_flLatestDataTime = pData->time();

		struct timeb now;
		now.time = 0;
		now.millitm = 0;

		ftime(&now);

		m_flTimeReceivedLatestData = (double)(now.time - m_iServerConnectionTimeS);
		m_flTimeReceivedLatestData += ((double)(now.millitm) - (double)m_iServerConnectionTimeMS)/1000;
	}
}

double CViewbackClient::PredictCurrentTime()
{
	struct timeb now;
	now.time = 0;
	now.millitm = 0;

	ftime(&now);

	double flTimeNow = (double)(now.time - m_iServerConnectionTimeS);
	flTimeNow += ((double)(now.millitm) - (double)m_iServerConnectionTimeMS) / 1000;

	double flTimeDifference = flTimeNow - m_flTimeReceivedLatestData;

	return m_flLatestDataTime + flTimeDifference;
}



