-- This sample shows how to use Orthanc to compress on-the-fly any
-- incoming DICOM file, as a JPEG2k file.

function OnStoredInstance(instanceId, tags, metadata, origin)
   -- Do not compress twice the same file
   if origin['RequestOrigin'] ~= 'Lua' then

      -- Retrieve the incoming DICOM instance from Orthanc
      local dicom = RestApiGet('/instances/' .. instanceId .. '/file')

      -- Write the DICOM content to some temporary file
      local uncompressed = instanceId .. '-uncompressed.dcm'
      local target = assert(io.open(uncompressed, 'wb'))
      target:write(dicom)
      target:close()

      -- Compress to JPEG2000 using gdcm
      local compressed = instanceId .. '-compressed.dcm'
      os.execute('gdcmconv -U --j2k ' .. uncompressed .. ' ' .. compressed)

      -- Generate a new SOPInstanceUID for the JPEG2000 file, as
      -- gdcmconv does not do this by itself
      os.execute('dcmodify --no-backup -gin ' .. compressed)

      -- Read the JPEG2000 file
      local source = assert(io.open(compressed, 'rb'))
      local jpeg2k = source:read("*all")
      source:close()

      -- Upload the JPEG2000 file and remove the uncompressed file
      RestApiPost('/instances', jpeg2k)
      RestApiDelete('/instances/' .. instanceId)

      -- Remove the temporary DICOM files
      os.remove(uncompressed)
      os.remove(compressed)
   end
end
