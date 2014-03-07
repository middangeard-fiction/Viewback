#include "viewback_client.h"

#include <sstream>

#include "../server/viewback_shared.h"

#include "viewback_servers.h"
#include "viewback_data.h"

using namespace std;

bool CViewbackClient::Initialize(ConsoleOutputCallback pfnConsoleOutput)
{
	m_pfnConsoleOutput = pfnConsoleOutput;

	return CViewbackServersThread::Run();
}

void CViewbackClient::Update()
{
	if (CViewbackDataThread::IsConnected())
	{
		vector<Packet> aPackets = CViewbackDataThread::GetData();

		// Look for a data registration packet.
		for (size_t i = 0; i < aPackets.size(); i++)
		{
			if (aPackets[i].data_registrations_size())
			{
				m_aDataRegistrations.resize(aPackets[i].data_registrations_size());
				m_aData.resize(aPackets[i].data_registrations_size());
				for (int j = 0; j < aPackets[i].data_registrations_size(); j++)
				{
					auto& oRegistrationProtobuf = aPackets[i].data_registrations(j);

					VBAssert(oRegistrationProtobuf.has_handle());
					VBAssert(oRegistrationProtobuf.has_field_name());
					VBAssert(oRegistrationProtobuf.has_type());
					VBAssert(oRegistrationProtobuf.handle() == j);

					auto& oRegistration = m_aDataRegistrations[oRegistrationProtobuf.handle()];
					oRegistration.m_iHandle = oRegistrationProtobuf.handle();
					oRegistration.m_sFieldName = oRegistrationProtobuf.field_name();
					oRegistration.m_eDataType = oRegistrationProtobuf.type();
				}

				VBPrintf("Installed %d registrations.\n", aPackets[i].data_registrations_size());

				for (int j = 0; j < aPackets[i].data_labels_size(); j++)
				{
					auto& oLabelProtobuf = aPackets[i].data_labels(j);

					VBAssert(oLabelProtobuf.has_handle());
					VBAssert(oLabelProtobuf.has_field_name());
					VBAssert(oLabelProtobuf.has_value());

					auto& oRegistration = m_aDataRegistrations[oLabelProtobuf.handle()];
					oRegistration.m_asLabels[oLabelProtobuf.value()] = oLabelProtobuf.field_name();
				}

				VBPrintf("Installed %d labels.\n", aPackets[i].data_labels_size());
			}
		}

		if (!m_aDataRegistrations.size())
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

		for (size_t i = 0; i < aPackets.size(); i++)
		{
			if (aPackets[i].has_data())
				StashData(&aPackets[i].data());

			if (aPackets[i].has_console_output() && m_pfnConsoleOutput)
				m_pfnConsoleOutput(aPackets[i].console_output().c_str());
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
	}
	else
	{
		unsigned long best_server = CViewbackServersThread::GetServer();

		if (best_server)
		{
			VBPrintf("Connecting to server at %u ...", best_server);

			bool bResult = CViewbackDataThread::Run(best_server);

			if (bResult)
				VBPrintf("Success.\n");
			else
				VBPrintf("Failed.\n");
		}
	}
}

bool CViewbackClient::HasConnection()
{
	return CViewbackDataThread::IsConnected();
}

void CViewbackClient::SendConsoleCommand(const string& sCommand)
{
	// This list is pumped into the data thread during the Update().
	m_sOutgoingCommands.push_back(sCommand);
}

vb_data_type_t CViewbackClient::TypeForHandle(size_t iHandle)
{
	return m_aDataRegistrations[iHandle].m_eDataType;
}

bool CViewbackClient::HasLabel(size_t iHandle, int iValue)
{
	auto& it = m_aDataRegistrations[iHandle].m_asLabels.find(iValue);
	if (it == m_aDataRegistrations[iHandle].m_asLabels.end())
		return false;
	else
		return true;
}

string CViewbackClient::GetLabelForValue(size_t iHandle, int iValue)
{
	auto& it = m_aDataRegistrations[iHandle].m_asLabels.find(iValue);
	if (it == m_aDataRegistrations[iHandle].m_asLabels.end())
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
}
