#include "StdAfx.h"
#include "ArpLayer.h"
#include <pcap.h>
#include <WinSock2.h>
#pragma  comment(lib, "ws2_32.lib")
#include <Packet32.h>
#pragma  comment(lib, "packet.lib")

CArpLayer::CArpLayer(char* pName)
	: CBaseLayer(pName)
{
	ResetHeader();
}

CArpLayer::~CArpLayer(void)
{
}

void CArpLayer::ResetHeader()
{
	m_sHeader.hard_type = 0x0100;
	m_sHeader.prot_type = 0x0008;
	m_sHeader.hard_size = 0x06;
	m_sHeader.prot_size = 0x04;
	m_sHeader.op = 0x0100;
	memset(m_sHeader.arp_src_macaddr.e_addr, 0, 6);
	memset(m_sHeader.arp_src_ipaddr.i_addr, 0, 4);
	memset(m_sHeader.arp_dst_macaddr.e_addr, 0, 6);
	memset(m_sHeader.arp_dst_ipaddr.i_addr, 0, 4);
}

BOOL CArpLayer::Receive(unsigned char* ppayload)
{
	PARP_HEADER pFrame = (PARP_HEADER)ppayload;
	BOOL bSuccess = FALSE;

	// 프록시테이블
	if (isProxyTableNotEmptyAndNotGarp(pFrame))
	{
		for (int i = 0; i < proxyTable.GetSize(); i++) {
			if (isProxyTableEntryAndPacketRequest(pFrame, i)) { // proxy ARP : 프록시 테이블 검사 (ip주소 있는지 & request이면)
				if (isInTableEntry(pFrame->arp_dst_ipaddr.S_un.s_ip_addr))	InsertTable(pFrame->arp_src_ipaddr, pFrame->arp_src_macaddr, true);
				pFrame = (PARP_HEADER)makeReplyPacket( (unsigned char*) pFrame, (ETHERNET_ADDR*)&proxyTable.GetAt(i).cache_enetaddr.e_addr, (IP_ADDR*)&proxyTable.GetAt(i).cache_ipaddr.i_addr);
				SendUnderLayerOp(pFrame,0x0200, ARP_MAX_LENGTH);
				return bSuccess;
			}
		}
	}

	if (memcmp(&pFrame->arp_src_ipaddr,&m_sHeader.arp_src_ipaddr, 4) != 0 ) 		// 패킷에 src == dest - 내가 보낸건가? { // 내 아이피 주소와 맞는지 확인
	{
		if (ntohs(pFrame->op) == 0x0001) {

/*확인할 것*/
			pFrame = (PARP_HEADER)makeReplyPacket( (unsigned char*) pFrame, (ETHERNET_ADDR*)&m_sHeader.arp_src_macaddr.e_addr, (IP_ADDR*)&m_sHeader.arp_src_ipaddr.i_addr);

			// 응답모드로 요청자에게 패킷 다시날림
			if(isInTableEntry(pFrame->arp_dst_ipaddr.S_un.s_ip_addr)) InsertTable(pFrame->arp_dst_ipaddr, pFrame->arp_dst_macaddr, true);
			
			if (isReceivePacketMine(pFrame)) {
				SendUnderLayerOp(pFrame,0x0200, ARP_MAX_LENGTH);
			}
		}

		else if (ntohs(pFrame->op) == 0x0002 ) { // reply												
			if (isReplyPacketARPorGARP(pFrame))		//GARP 0PCODE 0X0002
			{
				for (int i = 0; i < table.GetSize(); i++)
				{
					if (memcmp(&pFrame->arp_src_ipaddr, &table.GetAt(i).cache_ipaddr, 4) == 0)
					{
						memcpy(table.GetAt(i).cache_enetaddr.e_addr, pFrame->arp_src_macaddr.e_addr, 6);
						table.GetAt(i).cache_type = true;
						table.GetAt(i).cache_ttl = 0;
						mp_UnderLayer->SetDestinAddress(table.GetAt(i).cache_enetaddr.e_addr);
						mp_UnderLayer->SetSourceAddress(m_sHeader.arp_src_macaddr.S_un.s_ether_addr);
						mp_UnderLayer->setType(0x0008);
					}
				}
			}
			//여기까지요test
		}
	}
	else if (isPacketGARP(pFrame)) {
		if (ntohs(pFrame->op) == 0x0001) {
			if(memcmp(&pFrame->arp_src_macaddr, &m_sHeader.arp_src_macaddr, 6) != 0){ // garp request is not mine
				pFrame = (PARP_HEADER)makeReplyPacket( (unsigned char*) pFrame, (ETHERNET_ADDR*)&m_sHeader.arp_src_macaddr.e_addr, (IP_ADDR*)&m_sHeader.arp_src_ipaddr.i_addr);
				SendUnderLayerOp(pFrame,0x0200, ARP_MAX_LENGTH);
			}
		}

		if (ntohs(pFrame->op) == 0x0002) { 
			if (memcmp(&pFrame->arp_dst_macaddr, &m_sHeader.arp_src_macaddr, 6) == 0) { // garp reply is mine
				InsertTable(pFrame->arp_src_ipaddr, pFrame->arp_src_macaddr, true);
			}
		}
	}
	return bSuccess;
}

bool  CArpLayer::SendUnderLayerOp(PARP_HEADER pFrame, unsigned short op,int nlength)
{
	bool bSuccess = FALSE;
	pFrame->op = op;//타입재설정
	mp_UnderLayer->SetDestinAddress(pFrame->arp_dst_macaddr.e_addr);
	mp_UnderLayer->setType(ENET_TYPE_ARP);//Ethernet계층 타입설정
	bSuccess = mp_UnderLayer->Send((unsigned char*)pFrame, nlength);	
	return bSuccess;
}

bool CArpLayer::isPacketGARP(const PARP_HEADER  pFrame)
{
	return memcmp(&pFrame->arp_src_ipaddr, &pFrame->arp_dst_ipaddr, 4) == 0 &&
		memcmp(&pFrame->arp_dst_ipaddr, &m_sHeader.arp_src_ipaddr, 4) == 0;
}

bool CArpLayer::isReplyPacketARPorGARP(const PARP_HEADER pFrame)
{
	return !memcmp(&pFrame->arp_dst_ipaddr, &m_sHeader.arp_src_ipaddr, 4)
		|| memcmp(&pFrame->arp_dst_ipaddr, &pFrame->arp_src_ipaddr, 4);
}

bool CArpLayer::isProxyTableEntryAndPacketRequest(const PARP_HEADER pFrame, int i)
{
	return memcmp(pFrame->arp_dst_ipaddr.i_addr, proxyTable.GetAt(i).cache_ipaddr.i_addr, 4) == 0
		&& (ntohs(pFrame->op) == 0x0001);
}

bool CArpLayer::isProxyTableNotEmptyAndNotGarp(const PARP_HEADER pFrame)
{
	return !proxyTable.IsEmpty()
		&& memcmp(&pFrame->arp_src_ipaddr, &pFrame->arp_dst_ipaddr, 4) != 0;
}

bool CArpLayer::isReceivePacketMine(const PARP_HEADER pFrame)
{
	return memcmp(&pFrame->arp_dst_ipaddr, &m_sHeader.arp_src_ipaddr, 4) == 0;
}


//만약 캐시에 있어서 찾을경우 이더넷 설정해주고 true 반환
BOOL CArpLayer::Send(unsigned char* ppayload, int nlength)
{
	bool bsucess = FALSE;
	for (int i = 0; i<table.GetSize(); i++)
	{
		if (memcmp(&table.GetAt(i).cache_ipaddr, &m_sHeader.arp_dst_ipaddr, 4) == 0)//캐시테이블에서 검색후 존재한다면 그에 해당하는 맥주소 설정하여 전송
		{
			memcpy(m_sHeader.arp_data, ppayload, nlength);
			mp_UnderLayer->SetDestinAddress(table.GetAt(i).cache_enetaddr.e_addr);
			mp_UnderLayer->setType(0x0008);
			bsucess = mp_UnderLayer->Send((unsigned char*)&ppayload, nlength);
			return bsucess;
		}
	}
	memset(m_sHeader.arp_dst_macaddr.e_addr, 0xff, 6);		//목적지 Device Address를 Brodcast(0xff)로 설정함
	bsucess = SendUnderLayerOp(&m_sHeader, 0x0100, nlength + ARP_MAX_LENGTH);

	return false;
}

unsigned char* CArpLayer::makeReplyPacket(unsigned char* ppayload /*prame*/, ETHERNET_ADDR* macAddr, IP_ADDR* ipAddr) {	
	PARP_HEADER pFrame = (PARP_HEADER)ppayload;
	memcpy(pFrame->arp_dst_macaddr.e_addr, pFrame->arp_src_macaddr.e_addr, 6);
	memcpy(pFrame->arp_src_macaddr.e_addr, macAddr->S_un.s_ether_addr, 6);
	memcpy(pFrame->arp_dst_ipaddr.i_addr, pFrame->arp_src_ipaddr.i_addr, 4);
	memcpy(pFrame->arp_src_ipaddr.i_addr, ipAddr->S_un.s_ip_addr, 4);
	return (unsigned char*)pFrame;
}

void CArpLayer::SetSourceAddress(unsigned char* pAddress)
{
	memcpy(m_sHeader.arp_src_ipaddr.i_addr, pAddress, 4);
}

void CArpLayer::SetDestinAddress(unsigned char* pAddress)
{
	memcpy(m_sHeader.arp_dst_ipaddr.i_addr, pAddress, 4);
}
void CArpLayer::setSrcHd(unsigned char* pAddress)
{
	memcpy(m_sHeader.arp_src_macaddr.e_addr, pAddress, 6);
}

BOOL CArpLayer::InsertTable(IP_ADDR cache_ipaddr, ETHERNET_ADDR	cache_enetaddr,BOOL cache_type)
{
	CACHE_ENTRY cache;
	cache.cache_ipaddr = cache_ipaddr;
	cache.cache_enetaddr = cache_enetaddr;
	cache.cache_ttl = 0;
	cache.cache_type = cache_type;
	table.Add(cache);
	return true;
}

// 프록시 테이블 
BOOL CArpLayer::InsertProxyTable(IP_ADDR proxy_ipaddr, ETHERNET_ADDR proxyDeviceAddress)
{
	PROXY_ENTRY proxy;
	proxy.cache_ipaddr = proxy_ipaddr;
	proxy.cache_enetaddr = proxyDeviceAddress;
	proxyTable.Add(proxy);
	return true;
}

BOOL CArpLayer::isInTableEntry(unsigned char * pAddress)
{
	for (int i = 0; i < table.GetSize(); i++)
	{
		if (memcmp(&table[i].cache_ipaddr.i_addr,pAddress, 4) == 0 )
		{
			return false;
		}
	}
	return true;
}