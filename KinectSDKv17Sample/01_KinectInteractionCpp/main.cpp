#include <iostream>

#include <Windows.h>
#include <NuiApi.h>
#include <KinectInteraction.h>


#include <opencv2/opencv.hpp>



#define ERROR_CHECK( ret )  \
    if ( ret != S_OK ) {    \
    std::stringstream ss;	\
    ss << "failed " #ret " " << std::hex << ret << std::endl;			\
    throw std::runtime_error( ss.str().c_str() );			\
    }

const NUI_IMAGE_RESOLUTION CAMERA_RESOLUTION = NUI_IMAGE_RESOLUTION_640x480;

class KinectAdapter : public INuiInteractionClient
{
public:

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv)
    {
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef()
    {
        return 1;
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        return 0;
    }

    HRESULT STDMETHODCALLTYPE GetInteractionInfoAtLocation(DWORD skeletonTrackingId, NUI_HAND_TYPE handType, FLOAT x, FLOAT y, _Out_ NUI_INTERACTION_INFO *pInteractionInfo)
    {
        pInteractionInfo->IsGripTarget = TRUE;
        return S_OK;
    }
};

class KinectSample
{
private:

    INuiSensor* kinect;
    INuiInteractionStream* stream;
    KinectAdapter adapter;

    HANDLE imageStreamHandle;
    HANDLE depthStreamHandle;
    HANDLE streamEvent;

    DWORD width;
    DWORD height;

public:

    KinectSample()
    {
    }

    ~KinectSample()
    {
        // �I������
        if ( kinect != 0 ) {
            kinect->NuiShutdown();
            kinect->Release();
        }
    }

    void initialize()
    {
        createInstance();

        // Kinect�̐ݒ������������
        ERROR_CHECK( kinect->NuiInitialize( NUI_INITIALIZE_FLAG_USES_COLOR | NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX | NUI_INITIALIZE_FLAG_USES_SKELETON ) );

        // RGB�J����������������
        ERROR_CHECK( kinect->NuiImageStreamOpen( NUI_IMAGE_TYPE_COLOR, CAMERA_RESOLUTION,
            0, 2, 0, &imageStreamHandle ) );

        // �����J����������������
        ERROR_CHECK( kinect->NuiImageStreamOpen( NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX, CAMERA_RESOLUTION,
            0, 2, 0, &depthStreamHandle ) );

        // Near���[�h
        //ERROR_CHECK( kinect->NuiImageStreamSetImageFrameFlags(
        //  depthStreamHandle, NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE ) );

        // �X�P���g��������������
        ERROR_CHECK( kinect->NuiSkeletonTrackingEnable( 0, NUI_SKELETON_TRACKING_FLAG_ENABLE_SEATED_SUPPORT ) );

        // �t���[���X�V�C�x���g�̃n���h�����쐬����
        streamEvent = ::CreateEvent( 0, TRUE, FALSE, 0 );
        ERROR_CHECK( kinect->NuiSetFrameEndEvent( streamEvent, 0 ) );

        // �w�肵���𑜓x�́A��ʃT�C�Y���擾����
        ::NuiImageResolutionToSize(CAMERA_RESOLUTION, width, height );

        // �C���^���N�V�������C�u�����̏�����
        ERROR_CHECK( ::NuiCreateInteractionStream( kinect, &adapter, &stream ) );
        stream->Enable( 0 );
    }

    void run()
    {
        cv::Mat image;

        // ���C�����[�v
        while ( 1 ) {
            // �f�[�^�̍X�V��҂�
            DWORD ret = ::WaitForSingleObject( streamEvent, INFINITE );
            ::ResetEvent( streamEvent );

            drawRgbImage( image );
            processDepth();
            processSkeleton();
            processInteraction();

            // �摜��\������
            cv::imshow( "KinectSample", image );

            // �I���̂��߂̃L�[���̓`�F�b�N���A�\���̂��߂̃E�F�C�g
            int key = cv::waitKey( 10 );
            if ( key == 'q' ) {
                break;
            }
        }
    }

private:

    void createInstance()
    {
        // �ڑ�����Ă���Kinect�̐����擾����
        int count = 0;
        ERROR_CHECK( ::NuiGetSensorCount( &count ) );
        if ( count == 0 ) {
            throw std::runtime_error( "Kinect ��ڑ����Ă�������" );
        }

        // �ŏ���Kinect�̃C���X�^���X���쐬����
        ERROR_CHECK( ::NuiCreateSensorByIndex( 0, &kinect ) );

        // Kinect�̏�Ԃ��擾����
        HRESULT status = kinect->NuiStatus();
        if ( status != S_OK ) {
            throw std::runtime_error( "Kinect �����p�\�ł͂���܂���" );
        }
    }

    void drawRgbImage( cv::Mat& image )
    {
        // RGB�J�����̃t���[���f�[�^���擾����
        NUI_IMAGE_FRAME imageFrame = { 0 };
        ERROR_CHECK( kinect->NuiImageStreamGetNextFrame( imageStreamHandle, INFINITE, &imageFrame ) );

        // �摜�f�[�^���擾����
        NUI_LOCKED_RECT colorData;
        imageFrame.pFrameTexture->LockRect( 0, &colorData, 0, 0 );

        // �摜�f�[�^���R�s�[����
        image = cv::Mat( height, width, CV_8UC4, colorData.pBits );

        // �t���[���f�[�^���������
        ERROR_CHECK( kinect->NuiImageStreamReleaseFrame( imageStreamHandle, &imageFrame ) );
    }

    void processDepth()
    {
        // �����J�����̃t���[���f�[�^���擾����
        NUI_IMAGE_FRAME depthFrame = { 0 };
        ERROR_CHECK( kinect->NuiImageStreamGetNextFrame( depthStreamHandle, 0, &depthFrame ) );

        // �t���[���f�[�^�����ɁA�g�������f�[�^���擾����
		BOOL nearMode = FALSE;
		INuiFrameTexture *frameTexture = 0;
		kinect->NuiImageFrameGetDepthImagePixelFrameTexture( depthStreamHandle, &depthFrame, &nearMode, &frameTexture );

        // �����f�[�^���擾����
        NUI_LOCKED_RECT depthData = { 0 };
        frameTexture->LockRect( 0, &depthData, 0, 0 );
        if ( depthData.Pitch == 0 ) {
            std::cout << "zero" << std::endl;
        }

        // Depth�f�[�^��ݒ肷��
        ERROR_CHECK( stream->ProcessDepth( depthData.size, depthData.pBits, depthFrame.liTimeStamp ) );

        // �t���[���f�[�^���������
        ERROR_CHECK( kinect->NuiImageStreamReleaseFrame( depthStreamHandle, &depthFrame ) );
    }

    void processSkeleton()
    {
        // �X�P���g���̃t���[�����擾����
        NUI_SKELETON_FRAME skeletonFrame = { 0 };
        kinect->NuiSkeletonGetNextFrame( 0, &skeletonFrame );

        // �X�P���g���f�[�^��ݒ肷��
        Vector4 reading = { 0 };
        kinect->NuiAccelerometerGetCurrentReading( &reading );
        ERROR_CHECK( stream->ProcessSkeleton( NUI_SKELETON_COUNT, skeletonFrame.SkeletonData, &reading, skeletonFrame.liTimeStamp ) );
    }

    void processInteraction()
    {
        // �C���^���N�V�����t���[�����擾����
        NUI_INTERACTION_FRAME interactionFrame = { 0 } ;
        auto ret = stream->GetNextFrame( 0, &interactionFrame );
        if ( ret != S_OK ) {
            return;
        }

        for ( auto user : interactionFrame.UserInfos ) {
            if ( user.SkeletonTrackingId != 0 ) {
                for ( auto hand : user.HandPointerInfos ) {
                    std::cout << EventTypeToString( hand.HandEventType ) << " " << std::endl;
                }
            }
        }
    }

    std::string EventTypeToString( NUI_HAND_EVENT_TYPE eventType )
    {
        if ( eventType == NUI_HAND_EVENT_TYPE::NUI_HAND_EVENT_TYPE_GRIP ) {
            return "Grip";
        }
        else  if ( eventType == NUI_HAND_EVENT_TYPE::NUI_HAND_EVENT_TYPE_GRIPRELEASE ) {
            return "GripRelease";
        }

        return "None";
    }
};

void main()
{

    try {
        KinectSample kinect;
        kinect.initialize();
        kinect.run();
    }
    catch ( std::exception& ex ) {
        std::cout << ex.what() << std::endl;
    }
}