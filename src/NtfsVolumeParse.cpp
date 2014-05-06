#include "stdafx.h"
#include "NtfsVolumeParse.h"
#include <winioctl.h>
#include <iostream>
//#include "NTFS.h"

typedef struct _MULTI_SECTOR_HEADER
{
    UCHAR  Signature[4];
    USHORT UpdateSequenceArrayOffset;
    USHORT UpdateSequenceArraySize;

} MULTI_SECTOR_HEADER, *PMULTI_SECTOR_HEADER;

typedef struct _MFT_SEGMENT_REFERENCE
{
    ULONG  SegmentNumberLowPart;
    USHORT SegmentNumberHighPart;
    USHORT SequenceNumber;

} MFT_SEGMENT_REFERENCE, *PMFT_SEGMENT_REFERENCE;

typedef struct _UPDATE_SEQUENCE_ARRAY
{
    USHORT updateSequenceID;
    USHORT updateSequence1;
    USHORT updateSequence2;

} UPDATE_SEQUENCE_ARRAY, *PUPDATE_SEQUENCE_ARRAY;

typedef struct _FILE_RECORD_SEGMENT_HEADER
{
    MULTI_SECTOR_HEADER MultiSectorHeader;
    USN Lsn; /* $LogFile Sequence Number (LSN) */
    USHORT SequenceNumber; /* Sequence number */
    USHORT LinkCount; /* Hard link count */
    USHORT FirstAttributeOffset; /* Offset to the first attribute resident in the current file record. */
    USHORT Flags; /* Flags: 0x01 = in use, 0x02 = name index = directory, 0x04 or 0x08 = unknown. */
    ULONG BytesInUse; /* Used size for this FILE record */
    ULONG BytesAllocated; /* Total allocated size for this FILE record */
    MFT_SEGMENT_REFERENCE BaseFileRecordSegment; /* File reference to the base FILE record */
    USHORT NextAttributeNumber;
    USHORT Padding; /* for alignment to next 32-bit boundary */
    PUPDATE_SEQUENCE_ARRAY UpdateSequenceArray;

} FILE_RECORD_SEGMENT_HEADER, *PFILE_RECORD_SEGMENT_HEADER;

typedef enum
{
    //����ֻ�����浵���ļ����ԣ�
    //ʱ������ļ�����ʱ�����һ���޸�ʱ��
    //����Ŀ¼ָ����ļ���Ӳ���Ӽ���hard link count��
    AttributeStandardInformation = 0x10, //Resident_Attributes ��פ����

    //?????????????????????????????????
    //��һ���ļ�Ҫ����MFT�ļ���¼ʱ ���и�����
    //�����б��������ɸ��ļ�����Щ���ԣ��Լ�ÿ���������ڵ�MFT�ļ���¼���ļ�����
    //?????????????????????????????????
    AttributeAttributeList = 0x20,//��������ֵ���ܻ������������Ƿ�פ������

    //�ļ������Կ����ж����
    //1.���ļ����Զ�Ϊ����ļ���(�Ա�MS-DOS��16λ�������)
    //2.�����ļ�����Ӳ����ʱ
    AttributeFileName = 0x30, //��פ

    //һ���ļ���Ŀ¼��64�ֽڱ�ʶ�������е�16�ֽڶ��ڸþ���˵��Ψһ��
    //����-���ٷ��񽫶���ID�������ǿ�ݷ�ʽ��OLE����Դ�ļ���
    //NTFS�ṩ����Ӧ��API����Ϊ�ļ���Ŀ¼����ͨ�������ID��������ͨ�����ļ�����
    AttributeObjectId = 0x40, //��פ

    //Ϊ��NTFS��ǰ�汾����������
    //���о�����ͬ��ȫ���������ļ���Ŀ¼����ͬ���İ�ȫ����
    //��ǰ�汾��NTFS��˽�еİ�ȫ��������Ϣ��ÿ���ļ���Ŀ¼�洢��һ��
    AttributeSecurityDescriptor = 0x50,//������$SecureԪ�����ļ���

    //�����˸þ�İ汾��label��Ϣ
    AttributeVolumeName = 0x60, //��������$VolumeԪ�����ļ���
    AttributeVolumeInformation = 0x70,//��������$VolumeԪ�����ļ���

    //�ļ����ݣ�һ���ļ�����һ��δ�������������ԣ������ж�����������������
    //��һ���ļ������ж����������Ŀ¼û��Ĭ�ϵ��������ԣ������ж����ѡ����������������
    AttributeData = 0x80,//��������ֵ���ܻ������������Ƿ�פ������

    //������������ʵ�ִ�Ŀ¼���ļ��������λͼ����
    AttributeIndexRoot = 0x90,//��פ
    AttributeIndexAllocation = 0xA0,
    AttributeBitmap = 0xB0,

    //�洢��һ���ļ����ؽ��������ݣ�NTFS�Ľ���(junction)�͹��ص����������
    AttributeReparsePoint = 0xC0,

    //��������Ϊ��չ���ԣ����Ѳ��ٱ�����ʹ�ã�֮�����ṩ��Ϊ��OS/2���򱣳�������
    AttributeEAInformation = 0xD0,
    AttributeEA = 0xE0,

    AttributePropertySet = 0xF0,
    AttributeLoggedUtilityStream = 0x100,
    AttributeEnd = 0xFFFFFFFF
} ATTRIBUTE_TYPE, *PATTRIBUTE_TYPE;

typedef struct
{
    ATTRIBUTE_TYPE AttributeType;
    ULONG Length; //�����Գ��ȣ���������ֵ��
    BOOLEAN Nonresident; //�����Բ��� פ�� ����ô��
    UCHAR NameLength; //�����������Ƴ���
    USHORT NameOffset;//������ƫ��
    USHORT Flags; // 0x0001 ѹ�� 0x4000 ���� 0x8000ϡ���ļ�
    USHORT AttributeNumber;
} ATTRIBUTE, *PATTRIBUTE;

CNtfsVolumeParse::CNtfsVolumeParse()
{

}


CNtfsVolumeParse::~CNtfsVolumeParse(void)
{
}

LONGLONG CNtfsVolumeParse::GetRecordTotalSize(LPCTSTR lpszVolume)
{
    if(NULL == lpszVolume)
        return FALSE;

    TCHAR sVolumeName[MAX_PATH] = {0};
    _stprintf_s(sVolumeName, _T("\\\\.\\%c:"), lpszVolume[0]);

    HANDLE hVolume = CreateFile(sVolumeName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if(INVALID_HANDLE_VALUE == hVolume)
        return FALSE;

    NTFS_VOLUME_DATA_BUFFER ntfsVolData;
    DWORD dwWritten = 0;

    if(!DeviceIoControl(hVolume, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &ntfsVolData, sizeof(ntfsVolData), &dwWritten, NULL))
    {
        CloseHandle(hVolume);
        hVolume = INVALID_HANDLE_VALUE;
        return 0;
    }

    LONGLONG total_file_count = (ntfsVolData.MftValidDataLength.QuadPart / ntfsVolData.BytesPerFileRecordSegment);
    CloseHandle(hVolume);
    hVolume = INVALID_HANDLE_VALUE;
    return total_file_count;
}

BOOL CNtfsVolumeParse::ScanVolume(LPCTSTR lpszVolumeName)
{
    if(NULL == lpszVolumeName)
        return FALSE;

    TCHAR sVolumeName[MAX_PATH] = {0};
    _stprintf_s(sVolumeName, _T("\\\\.\\%c:"), lpszVolumeName[0]);

    HANDLE hVolume = CreateFile(sVolumeName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if(INVALID_HANDLE_VALUE == hVolume)
        return FALSE;

    USN_JOURNAL_DATA journalData;
    DWORD dwBytes = 0;

    if(!DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &dwBytes, NULL))
    {
        CloseHandle(hVolume);
        hVolume = INVALID_HANDLE_VALUE;
        return FALSE;
    }

    NTFS_VOLUME_DATA_BUFFER ntfsVolData;
    DWORD dwWritten = 0;

    BOOL bDioControl = DeviceIoControl(hVolume, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &ntfsVolData, sizeof(ntfsVolData), &dwWritten, NULL);

    if(!bDioControl)
    {
        CloseHandle(hVolume);
        hVolume = INVALID_HANDLE_VALUE;
        return FALSE;
    }

    DWORD dwFileRecordOutputBufferSize = sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) + ntfsVolData.BytesPerFileRecordSegment - 1;
    PNTFS_FILE_RECORD_OUTPUT_BUFFER ntfsFileRecordOutput = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)malloc(dwFileRecordOutputBufferSize);
    LONGLONG fileTotalCount = GetRecordTotalSize(lpszVolumeName);

    for(LONGLONG i = 0; i < fileTotalCount; i++)
    {
        NTFS_FILE_RECORD_INPUT_BUFFER mftRecordInput;
        mftRecordInput.FileReferenceNumber.QuadPart = i;

        memset(ntfsFileRecordOutput, 0, dwFileRecordOutputBufferSize);

        if(!DeviceIoControl(hVolume, FSCTL_GET_NTFS_FILE_RECORD, &mftRecordInput, sizeof(mftRecordInput), ntfsFileRecordOutput, dwFileRecordOutputBufferSize, &dwWritten, NULL))
        {
            CloseHandle(hVolume);
            hVolume = INVALID_HANDLE_VALUE;
            return FALSE;
        }

        PFILE_RECORD_SEGMENT_HEADER pfileRecordheader = (PFILE_RECORD_SEGMENT_HEADER)ntfsFileRecordOutput->FileRecordBuffer;

        UCHAR* headerSignature = pfileRecordheader->MultiSectorHeader.Signature;

        if(headerSignature[0] != 'F' || headerSignature[1] != 'I' || headerSignature[2] != 'L' || headerSignature[3] != 'E')
            continue;

        for(PATTRIBUTE pAttribute = (PATTRIBUTE)((PBYTE)pfileRecordheader + pfileRecordheader->FirstAttributeOffset); pAttribute->AttributeType != -1; pAttribute = (PATTRIBUTE)((PBYTE)pAttribute + pAttribute->Length))
        {

        }
    }

    free(ntfsFileRecordOutput);

    CloseHandle(hVolume);
    hVolume = INVALID_HANDLE_VALUE;

    return TRUE;
}

BOOL CNtfsVolumeParse::ScanFileChange(LPCTSTR lpszVolume)
{
    if(NULL == lpszVolume)
        return FALSE;

    TCHAR sVolumeName[MAX_PATH] = {0};
    _stprintf_s(sVolumeName, _T("\\\\.\\%c:"), lpszVolume[0]);

    HANDLE hVolume = CreateFile(sVolumeName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if(INVALID_HANDLE_VALUE == hVolume)
        return FALSE;

    USN_JOURNAL_DATA journalData;
    DWORD dwBytes = 0;

    if(!DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &dwBytes, NULL))
    {
        CloseHandle(hVolume);
        hVolume = INVALID_HANDLE_VALUE;
        return FALSE;
    }

    MFT_ENUM_DATA med;
    med.StartFileReferenceNumber = 0;
    med.LowUsn = 0;
    med.HighUsn = journalData.NextUsn;

    PUSN_RECORD usnRecord;
    DWORD dwRetBytes;
    char buffer[USN_PAGE_SIZE];
    BOOL bDioControl = FALSE;
    WCHAR sFileName[USN_PAGE_SIZE] = {0};

    for(;;)
    {
        memset(buffer, 0, sizeof(USN_PAGE_SIZE));
        bDioControl = DeviceIoControl(hVolume, FSCTL_ENUM_USN_DATA, &med, sizeof(med), &buffer, USN_PAGE_SIZE, &dwBytes, NULL);

        if(!bDioControl)
        {
            CloseHandle(hVolume);
            hVolume = INVALID_HANDLE_VALUE;
            break;
        }

        if(dwBytes <= sizeof(USN)) break;

        dwRetBytes = dwBytes - sizeof(USN);
        usnRecord = (PUSN_RECORD)(((PUCHAR)buffer) + sizeof(USN));

        while(dwRetBytes > 0)
        {
            _tsetlocale(LC_ALL, _T("chs"));
            swprintf_s(sFileName, L"%.*ws\r\n", (int)(usnRecord->FileNameLength / 2), usnRecord->FileName);
            wprintf(sFileName);
            dwRetBytes -= usnRecord->RecordLength;
            usnRecord = (PUSN_RECORD)(((PCHAR)usnRecord) + usnRecord->RecordLength);
        }

        med.StartFileReferenceNumber = *(DWORDLONG*)buffer;
    }

    CloseHandle(hVolume);
    hVolume = INVALID_HANDLE_VALUE;

    return TRUE;
}
