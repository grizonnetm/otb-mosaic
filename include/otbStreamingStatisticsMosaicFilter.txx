#ifndef __StreamingStatisticsMosaicFilter_txx
#define __StreamingStatisticsMosaicFilter_txx

#include "otbStreamingStatisticsMosaicFilter.h"

namespace otb {

template <class TInputImage, class TOutputImage, class TInternalValueType>
StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>
::StreamingStatisticsMosaicFilter(){
}

/*
 * Extra initialization
 */
template <class TInputImage, class TOutputImage, class TInternalValueType>
void
StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>
::GenerateOutputInformation()
{
  Superclass::GenerateOutputInformation();

  OutputImageType * outputPtr = this->GetOutput();

  outputPtr->SetNumberOfComponentsPerPixel( 1 );

  // Prepare final results
  unsigned int nBands = Superclass::GetNumberOfBands();
  unsigned int nbImages = this->GetNumberOfInputImages();
  m_FinalResults = ThreadResultsContainer(nBands,nbImages*nbImages);

}

/**
 * Processing
 */
template <class TInputImage, class TOutputImage, class TInternalValueType>
void
StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>
::ThreadedGenerateData(const OutputImageRegionType& outputRegionForThread, itk::ThreadIdType threadId)
{

  // Debug info
  itkDebugMacro(<<"Actually executing thread " << threadId << " in region " << outputRegionForThread);

  // Support progress methods/callbacks
  itk::ProgressReporter progress(this, threadId, outputRegionForThread.GetNumberOfPixels() );

  // Get number of input images
  const unsigned int nbOfInputImages = this->GetNumberOfInputImages();

  // Get number of used inputs
  const unsigned int nbOfUsedInputImages = Superclass::GetNumberOfUsedInputImages();

  // Iterate through the thread region
  IteratorType outputIt(this->GetOutput(), outputRegionForThread);

  // Prepare input pointers, interpolators, and valid regions (input images)
  typename std::vector<InputImageType *>        currentImage;
  typename std::vector<InterpolatorPointerType> interp;
  Superclass::PrepareImageAccessors(currentImage, interp);

  // temporary variables
  OutputImagePointType geoPoint;

  for ( outputIt.GoToBegin(); !outputIt.IsAtEnd(); ++outputIt )
    {

    // Update progress
    progress.CompletedPixel();

    // Overlap descriptor for the current pixel (yes/no + value)
    std::vector<unsigned int>        overlapImagesIndices;
    std::vector<InputImagePixelType> overlapPixelValue;

    // Current pixel --> Geographical point
    this->GetOutput()->TransformIndexToPhysicalPoint (outputIt.GetIndex(), geoPoint) ;

    // Loop on used input images
    for (unsigned int i = 0 ; i < nbOfUsedInputImages ; i++)
      {

      // Check if the point is inside the transformed thread region
      // (i.e. the region in the current input image which match the thread
      // region)
      if (interp[i]->IsInsideBuffer(geoPoint) )
        {

        // Compute the interpolated pixel value
        InputImagePixelType interpolatedPixel = interp[i]->Evaluate(geoPoint);

        // Put image index + image pixel value into memory
        if (Superclass::IsPixelNotEmpty(interpolatedPixel) )
          {
          overlapImagesIndices.push_back (Superclass::GetUsedInputImageIndice(i) );
          overlapPixelValue.push_back(interpolatedPixel);
          }

        } // point inside buffer
      }   // next image

    // Update thread result

    // Nb of overlaps at the current pixel
    unsigned int nbOfOverlappingPixels = overlapImagesIndices.size();

    // Loop on overlapping pixels
    for (unsigned int i = 0 ; i < nbOfOverlappingPixels ; i++)
      {
      // Index of the image whose comes the current overlapping pixel
      unsigned int imageIndex = overlapImagesIndices.at(i);

      // We need to sum this pixel to all overlaps ij
      InputImagePixelType pixel = overlapPixelValue.at(i);

      for (unsigned int j = 0 ; j < nbOfOverlappingPixels ; j++)
        {
        //				if (i!=j)
          {
          // Index of the other image which share this overlapping pixel
          unsigned int otherImageIndex = overlapImagesIndices.at(j);

          // Pixel value of the other image which share this overlapping pixel
          InputImagePixelType otherPixel = overlapPixelValue.at(j);

          // Update
          //					m_ThreadResults.at(threadId).Update(pixel,
          // imageIndex*nbImages + otherImageIndex);
          m_ThreadResults.at(threadId).Update(pixel, otherPixel, imageIndex*nbOfInputImages + otherImageIndex);
          }
        }
      } // loop on overlapping pixels

    OutputImagePixelType outPix;
    outPix.SetSize(1);
    outPix.Fill( static_cast<OutputImageInternalPixelType>(nbOfOverlappingPixels) );
    outputIt.Set(outPix);

    } // next output pixel

}

/**
 * Setup state of filter before multi-threading.
 * InterpolatorType::SetInputImage is not thread-safe and hence
 * has to be setup before ThreadedGenerateData
 *
 * Each thread result container must be cleared
 */
template <class TInputImage, class TOutputImage, class TInternalValueType>
void
StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>
::BeforeThreadedGenerateData()
{

  Superclass::BeforeThreadedGenerateData();

  // Prepare threads result
  const unsigned int numberOfThreads = this->GetNumberOfThreads();
  const unsigned int nbImages = this->GetNumberOfInputs();
  const unsigned int nBands = this->GetNumberOfBands();

  m_ThreadResults.clear();
  for (unsigned int threadId = 0 ; threadId < numberOfThreads ; threadId++)
    {
    // Create a clean empty container for each thread
    ThreadResultsContainer threadResult(nBands,nbImages*nbImages);
    m_ThreadResults.push_back(threadResult);
    }

}

/**
 * Setup state of filter after multi-threading.
 */
template <class TInputImage, class TOutputImage, class TInternalValueType>
void
StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>
::AfterThreadedGenerateData()
{

  Superclass::AfterThreadedGenerateData();

  // Merge threads result
  const unsigned int numberOfThreads = this->GetNumberOfThreads();

  for (unsigned int threadId = 0 ; threadId < numberOfThreads ; threadId++)
    {
    m_FinalResults.Update(m_ThreadResults.at(threadId) );
    }

}

/*
 * Get Mean matrix
 */
template <class TInputImage, class TOutputImage, class TInternalValueType>
vnl_matrix<typename StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>::InternalValueType>
StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>
::GetMean(unsigned int band)
{
  const unsigned int nbImages = this->GetNumberOfInputs();

  vnl_matrix<InternalValueType> res(nbImages,nbImages,0);
  for (unsigned int i = 0 ; i < nbImages ; i++)
    {
    for (unsigned int j = 0 ; j < nbImages ; j++)
      {
      InternalValueType sum = m_FinalResults.m_sum[band][i*nbImages+j];
      long              count = m_FinalResults.m_count[i*nbImages+j];
      if (count > 0)
        {
        res[i][j] = sum / (static_cast<InternalValueType>(count) );
        }
      }
    }
  return res;
}

/*
 * Get Product Mean matrix
 */
template <class TInputImage, class TOutputImage, class TInternalValueType>
vnl_matrix<typename StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>::InternalValueType>
StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>
::GetProdMean(unsigned int band)
{
  const unsigned int nbImages = this->GetNumberOfInputs();

  vnl_matrix<InternalValueType> res(nbImages,nbImages,0);
  for (unsigned int i = 0 ; i < nbImages ; i++)
    {
    for (unsigned int j = 0 ; j < nbImages ; j++)
      {
      InternalValueType cosum = m_FinalResults.m_cosum[band][i*nbImages+j];
      long              count = m_FinalResults.m_count[i*nbImages+j];
      if (count > 0)
        {
        res[i][j] = cosum / (static_cast<InternalValueType>(count) );
        }
      }
    }
  return res;
}

/*
 * Get Standard Deviation matrix
 */
template <class TInputImage, class TOutputImage, class TInternalValueType>
vnl_matrix<typename StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>::InternalValueType>
StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>
::GetStDev(unsigned int band)
{
  const unsigned int nbImages = this->GetNumberOfInputs();

  vnl_matrix<InternalValueType> res(nbImages,nbImages,0);
  for (unsigned int i = 0 ; i < nbImages ; i++)
    {
    for (unsigned int j = 0 ; j < nbImages ; j++)
      {
      InternalValueType sum = m_FinalResults.m_sum[band][i*nbImages+j];
      InternalValueType sqSum = m_FinalResults.m_sqSum[band][i*nbImages+j];
      long              count = m_FinalResults.m_count[i*nbImages+j];
      if (count > 1)
        {
        // Unbiased estimate
        InternalValueType variance = (sqSum - (sum*sum
                                               / static_cast<InternalValueType>(count) ) )
          / (static_cast<InternalValueType>(count) - 1);
        if (variance > 0)
          {
          res[i][j] = vcl_sqrt(variance);
          }
        }
      }
    }
  return res;
}

/*
 * Get Minimums
 */
template <class TInputImage, class TOutputImage, class TInternalValueType>
vnl_vector<typename StreamingStatisticsMosaicFilter<TInputImage, TOutputImage,
                                                    TInternalValueType>::InputImageInternalPixelType>
StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>
::GetMin(unsigned int band)
{
  const unsigned int nbImages = this->GetNumberOfInputs();

  vnl_vector<InputImageInternalPixelType> res(nbImages,itk::NumericTraits<InputImageInternalPixelType>::max() );
  for (unsigned int i = 0 ; i < nbImages ; i++)
    {
    for (unsigned int j = 0 ; j < nbImages ; j++)
      {
      InputImageInternalPixelType value = m_FinalResults.m_min[band][i*nbImages+j];
      if (value < res[i])
        res[i] = value;
      }
    }
  return res;
}

/*
 * Get Maximums
 */
template <class TInputImage, class TOutputImage, class TInternalValueType>
vnl_vector<typename StreamingStatisticsMosaicFilter<TInputImage, TOutputImage,
                                                    TInternalValueType>::InputImageInternalPixelType>
StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>
::GetMax(unsigned int band)
{
  const unsigned int nbImages = this->GetNumberOfInputs();

  vnl_vector<InputImageInternalPixelType> res(nbImages,
                                              itk::NumericTraits<InputImageInternalPixelType>::NonpositiveMin() );
  for (unsigned int i = 0 ; i < nbImages ; i++)
    {
    for (unsigned int j = 0 ; j < nbImages ; j++)
      {
      InputImageInternalPixelType value = m_FinalResults.m_max[band][i*nbImages+j];
      if (value > res[i])
        res[i] = value;
      }
    }
  return res;
}

/*
 * Get Area matrix
 */
template <class TInputImage, class TOutputImage, class TInternalValueType>
vnl_matrix<long>
StreamingStatisticsMosaicFilter<TInputImage, TOutputImage, TInternalValueType>
::GetAreaInPixels()
{
  const unsigned int nbImages = this->GetNumberOfInputs();

  vnl_matrix<long> res(nbImages,nbImages,0);
  for (unsigned int i = 0 ; i < nbImages ; i++)
    {
    for (unsigned int j = 0 ; j < nbImages ; j++)
      {
      res[i][j] = m_FinalResults.m_count[i*nbImages+j];
      }
    }
  return res;
}

} // end namespace gtb

#endif
