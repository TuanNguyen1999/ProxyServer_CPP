#include "pch.h"
#include "CSockThread.h"
#include "Server.h"
#include <fstream>
#include <chrono>

IMPLEMENT_DYNCREATE(CSockThread, CWinThread)

BOOL CSockThread::InitInstance()
{
	m_sConnected.Attach(m_hConnected);
	m_hConnected = NULL;

	//AfxMessageBox(L"A Socket thread created");

	return TRUE;
}
int CSockThread::ExitInstance()
{
	//AfxMessageBox(L"A Socket thread destroyed");

	m_sConnected.Close();
	return CWinThread::ExitInstance();
}

int CSockThread::CacheData(const char* fileName, const char* buffer, int len, bool app)
{
	std::string path = FOLDER_CACHE + '/' + fileName;

	int openMode = app ? std::ios_base::app : std::ios_base::out;	// append or overwrite

	std::ofstream writeStream(path, openMode);
	if (!writeStream.is_open()) 
	{
		//File is being used by another process
		writeStream.close();
		return -1;
	}

	writeStream.write(buffer, len);
	writeStream.close();

	return len;
}


std::string CSockThread::ReFormatUri(const CStringA& str)
{
	auto ToHyphen = [&](char c)
	{
		if (c == '\\' || c == '/' || c == ':') c = '-';
		return	c;
	};

	int i = 0;
	i = str.Find("http://");

	i = (i >= 0) ? i + 7 : 0;


	std::string result;

	// Convert '/', '\', ':' to hyphen '-'
	for (i; i < str.GetLength(); i++)
	{
		result.push_back(ToHyphen(str[i]));
	}

	return result;
}



int CSockThread::Run()
{
	int len = 100, nByteReceived = 0;
	CHAR* buffer = new CHAR[len];
	CStringA request, response;
	time_t today;
	bool bCanCache = false;
	while (true)
	{
		bCanCache = true;
		today = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		request = "";
		response = "";

		do
		{

			memset(buffer, 0, sizeof(CHAR) * len);

			//Waiting to receive request from client
			nByteReceived = m_sConnected.Receive(buffer, len);	//Blocking call

			if(nByteReceived > 0) request.Append(buffer, nByteReceived);

		} while (nByteReceived == len);
		request += '\0';
		if (nByteReceived <= 0) break;

		//Parse request from client
		HttpHeader httpHeader = m_sConnected.RequestParser(request);

		// Check for supported method
		if (httpHeader.Method != "GET" && httpHeader.Method != "POST")
		{
			response = httpHeader.HttpVer;
			response += " 501 Not Implemented\r\n";
			m_sConnected.Send(response.GetString(), response.GetLength());
			break;
		}

		// Check for Forbidden domains
		bool bIsForbidden = false;
		for (auto it = vBlackList.begin(); it != vBlackList.end(); it++)
		{
			if (*it == httpHeader.Host)
			{
				bIsForbidden = true;
				break;
			}
		}

		// Send forbidden response to client if forbidden flag is true
		if (bIsForbidden)
		{
			response = httpHeader.HttpVer;
			response += " 403 Forbidden\r\n";
			m_sConnected.Send(response.GetString(), response.GetLength());
			break;
		}

		std::string file = FOLDER_CACHE + '/' + ReFormatUri(httpHeader.Uri);

		std::string time_file = file + ".time";

		std::ifstream readStream(time_file, std::ios_base::binary);
		if (readStream.is_open())
		{
			time_t dateCreated = 0;
			readStream >> dateCreated;
			if (today - dateCreated <= CACHE_MAX_AGE)
			{
				std::string data_file = file + ".data";
				std::ifstream readStream(data_file, std::ios_base::binary);

				if (readStream.is_open())
				{
					//AfxMessageBox(L"Get data from file");
					while (!readStream.eof())
					{
						memset(buffer, 0, len);
						readStream.read(buffer, len);
						m_sConnected.Send(buffer, len);
					}
					continue;
				}
			}
		}


		httpHeader.Port = (httpHeader.Port == "") ? "80" : httpHeader.Port;
		//Create a socket to send request to server

		CRequestHandlerSock temp;
		temp.Create();
		if (temp.Connect(CString(httpHeader.Host), StrToIntA(httpHeader.Port)) == TRUE)
		{
			std::ofstream writeStream;
			// Write created time
			writeStream = std::ofstream(file + ".time", std::ios_base::binary);
			writeStream << today;
			writeStream.close();


			// Send request to server
			temp.Send(request.GetString(), request.GetLength());

			writeStream = std::ofstream(file + ".data", std::ios_base::binary);	//Caching data to file

			//  receive response from server
			do
			{
				memset(buffer, 0, sizeof(CHAR) * len);
				nByteReceived = temp.Receive(buffer, len);	//Blocking call

				// Redirect response to client
				if (nByteReceived > 0)
				{
					writeStream.write(buffer, nByteReceived);
					m_sConnected.Send(buffer, nByteReceived);
				}
			} while (nByteReceived > 0);

			temp.Close();
			writeStream.close();
		}
	}

	delete[] buffer;
	return ExitInstance();
}
