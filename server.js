const express = require('express');
const path = require('path');
const fs = require('fs');

const app = express();
const port = 80; // Port number to listen on

// Ensure the uploads directory and device-specific directories exist
const ensureDirectoryExists = (dir) => {
    if (!fs.existsSync(dir)) {
        fs.mkdirSync(dir, { recursive: true });
    }
};

const device1Dir = path.join(__dirname, 'uploads', 'device1');
const device2Dir = path.join(__dirname, 'uploads', 'device2');
ensureDirectoryExists(device1Dir);
ensureDirectoryExists(device2Dir);

// Serve static files from the uploads directory
app.use('/files', express.static('uploads'));

// Endpoint to check for new audio files
app.get('/check/:device', (req, res) => {
    const device = req.params.device;
    const deviceDir = device === 'device1' ? device1Dir : device2Dir;

    try {
        // Read files from the directory and filter out .DS_Store and other hidden files
        const files = fs.readdirSync(deviceDir).filter(file => !file.startsWith('.'));

        console.log(`Checking for new audio files in ${deviceDir}...`);
        console.log('Available files:', files);

        if (files.length > 0) {
            console.log('New audio file(s) found.');
            res.status(200).send('New audio file(s) found.');
        } else {
            console.log('No new audio files found.');
            res.status(404).send('No new audio files found.');
        }
    } catch (error) {
        console.error('Error checking for new audio files:', error);
        res.status(500).send('Error checking for new audio files.');
    }
});   


// Endpoint to handle file upload
app.post('/upload', (req, res) => {
    try {
        console.log('Received upload request');
        console.log('Headers:', req.headers);
        console.log('Content-Type:', req.get('Content-Type'));

        const contentType = req.get('Content-Type');
        const deviceType = req.get('X-Device-Type') || 'device1';
        const filename = req.get('X-Filename') || 'default_audio.wav'; // Extract filename from header

        const deviceDir = deviceType === 'device1' ? device1Dir : device2Dir;

        const filePath = path.join(deviceDir, filename); // Save file with the provided filename
        const fileStream = fs.createWriteStream(filePath);

        let totalBytesWritten = 0;

        req.on('data', (chunk) => {
            totalBytesWritten += chunk.length;
            fileStream.write(chunk);
        });

        req.on('end', () => {
            fileStream.end();
            console.log(`File uploaded successfully: ${filePath}`);
            console.log(`Total bytes written: ${totalBytesWritten}`);
            res.status(200).send(`File uploaded successfully as ${filename}`);
        });

        req.on('error', (err) => {
            console.error('Error during file upload:', err);
            res.status(500).send('Error during file upload');
        });
    } catch (error) {
        console.error('Error processing upload:', error);
        res.status(500).send('Error processing upload');
    }
});

// Endpoint to handle file download and deletion after download
app.get('/download/:device/:filename', (req, res) => {
    const device = req.params.device;
    const filename = req.params.filename;

    // Determine the correct directory based on the device parameter
    const deviceDir = device === 'device1' ? device1Dir : device2Dir;
    
    // Construct the correct file path
    const filePath = path.join(deviceDir, filename);

    console.log('Received download request');
    console.log('Requested file:', filename);
    console.log('File path:', filePath);

    if (fs.existsSync(filePath)) {
        console.log('File found, sending...');
        res.sendFile(filePath, (err) => {
            if (err) {
                console.error('Failed to send file:', err);
                res.status(500).send('Failed to send file.');
            } else {
                console.log('File sent successfully:', filePath);

                // Optionally, delete the file after sending it
                fs.unlink(filePath, (unlinkErr) => {
                    if (unlinkErr) {
                        console.error('Failed to delete file after download:', unlinkErr);
                    } else {
                        console.log('File deleted after download:', filePath);
                    }
                });
            }
        });
    } else {
        console.log('File not found:', filePath);
        res.status(404).send('File not found.');
    }
});


app.listen(port, () => {
    console.log(`Server running on http://localhost:${port}`);
});

