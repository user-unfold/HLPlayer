#ifndef HLPLAYER_IPIPELINENODE_H
#define HLPLAYER_IPIPELINENODE_H

#include <hlplayer/Result.h>
#include <hlplayer/GpuFrameContract.h>
#include <cstdint>
#include <string>

namespace hlplayer {

/// Base interface for all pipeline processing nodes.
/// A pipeline node can be a decoder, AI processor, encoder, or any other
/// transformation unit in the video processing pipeline.
class HLPLAYER_CORE_API IPipelineNode {
public:
    virtual ~IPipelineNode() = default;

    /// Initialize the pipeline node with configuration.
    /// @return Result<void>::success() on success, or an error code on failure.
    virtual Result<void> initialize() = 0;

    /// Process a single GPU frame through the pipeline node.
    /// @param frame Input GPU frame to process.
    /// @return Result<GpuFrame> containing the processed frame, or an error.
    virtual Result<GpuFrame> process(const GpuFrame& frame) = 0;

    /// Flush any buffered data and return remaining frames.
    /// @return Result<std::vector<GpuFrame>> containing any remaining frames, or an error.
    virtual Result<std::vector<GpuFrame>> flush() = 0;

    /// Reset the pipeline node to initial state, clearing all buffers.
    virtual void reset() = 0;

    /// Get the name of this pipeline node for debugging and logging.
    /// @return Name of the pipeline node.
    virtual std::string nodeName() const = 0;

    /// Check if the pipeline node is in a healthy operational state.
    /// @return true if healthy, false if degraded or failed.
    virtual bool isHealthy() const = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_IPIPELINENODE_H
