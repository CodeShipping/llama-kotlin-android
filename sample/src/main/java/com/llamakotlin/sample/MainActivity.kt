package com.llamakotlin.sample

import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.llamakotlin.android.LlamaModel
import com.llamakotlin.android.exception.LlamaException
import com.llamakotlin.sample.databinding.ActivityMainBinding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private lateinit var prefs: SharedPreferences
    private var model: LlamaModel? = null
    private var generationJob: Job? = null

    companion object {
        private const val PREFS_NAME = "llama_prefs"
        private const val KEY_MODEL_PATH = "model_path"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        try {
            binding = ActivityMainBinding.inflate(layoutInflater)
            setContentView(binding.root)
            
            prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

            setupUI()
            showVersion()
            
            // Auto-load last used model
            autoLoadModel()
        } catch (e: Exception) {
            e.printStackTrace()
            Toast.makeText(this, "Error: ${e.message}", Toast.LENGTH_LONG).show()
        }
    }
    
    private fun autoLoadModel() {
        val savedPath = prefs.getString(KEY_MODEL_PATH, null)
        if (savedPath != null) {
            val modelFile = File(savedPath)
            if (modelFile.exists()) {
                binding.tvStatus.text = "Auto-loading last model..."
                loadModel(savedPath)
            } else {
                // Clear saved path if file no longer exists
                prefs.edit().remove(KEY_MODEL_PATH).apply()
                binding.tvStatus.text = "Previous model not found. Please select a new model."
            }
        }
    }

    private fun setupUI() {
        binding.btnLoadModel.setOnClickListener {
            pickModelFile()
        }

        binding.btnGenerate.setOnClickListener {
            val prompt = binding.etPrompt.text.toString()
            if (prompt.isBlank()) {
                Toast.makeText(this, "Please enter a prompt", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            generateResponse(prompt)
        }

        binding.btnCancel.setOnClickListener {
            cancelGeneration()
        }

        binding.btnClear.setOnClickListener {
            binding.tvResponse.text = ""
        }
    }

    private fun showVersion() {
        try {
            binding.tvStatus.text = "Library: ${LlamaModel.getVersion()}"
        } catch (e: UnsatisfiedLinkError) {
            binding.tvStatus.text = "Native library not loaded"
        }
    }

    // File picker for GGUF models
    private val filePickerLauncher = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri?.let { selectedUri ->
            // Take persistent permission
            contentResolver.takePersistableUriPermission(
                selectedUri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
            copyAndLoadModel(selectedUri)
        }
    }

    private fun pickModelFile() {
        binding.tvStatus.text = "Opening file picker...\nSelect a .gguf model file"
        
        // Use Storage Access Framework - works on all Android versions
        filePickerLauncher.launch(arrayOf("*/*"))
    }

    private fun copyAndLoadModel(uri: Uri) {
        binding.tvStatus.text = "Copying model to app storage..."
        binding.btnLoadModel.isEnabled = false

        lifecycleScope.launch(Dispatchers.IO) {
            try {
                // Get file name
                val fileName = getFileName(uri) ?: "model.gguf"
                
                if (!fileName.endsWith(".gguf", ignoreCase = true)) {
                    withContext(Dispatchers.Main) {
                        binding.tvStatus.text = "Please select a .gguf file"
                        binding.btnLoadModel.isEnabled = true
                    }
                    return@launch
                }

                // Copy to app's internal storage
                val modelFile = File(filesDir, fileName)
                
                withContext(Dispatchers.Main) {
                    binding.tvStatus.text = "Copying $fileName...\nThis may take a few minutes for large models."
                }

                contentResolver.openInputStream(uri)?.use { input ->
                    FileOutputStream(modelFile).use { output ->
                        val buffer = ByteArray(8192)
                        var bytesRead: Int
                        var totalBytes = 0L
                        while (input.read(buffer).also { bytesRead = it } != -1) {
                            output.write(buffer, 0, bytesRead)
                            totalBytes += bytesRead
                            // Update progress every 10MB
                            if (totalBytes % (10 * 1024 * 1024) < 8192) {
                                withContext(Dispatchers.Main) {
                                    binding.tvStatus.text = "Copying $fileName...\n${totalBytes / (1024 * 1024)} MB copied"
                                }
                            }
                        }
                    }
                }

                withContext(Dispatchers.Main) {
                    binding.tvStatus.text = "Model copied. Loading..."
                    binding.btnLoadModel.isEnabled = true
                    loadModel(modelFile.absolutePath)
                }

            } catch (e: Exception) {
                e.printStackTrace()
                withContext(Dispatchers.Main) {
                    binding.tvStatus.text = "Error copying model: ${e.message}"
                    binding.btnLoadModel.isEnabled = true
                }
            }
        }
    }

    private fun getFileName(uri: Uri): String? {
        var name: String? = null
        contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (cursor.moveToFirst() && nameIndex >= 0) {
                name = cursor.getString(nameIndex)
            }
        }
        return name ?: uri.lastPathSegment
    }

    private fun loadModel(modelPath: String) {
        binding.tvStatus.text = "Loading model..."
        binding.btnLoadModel.isEnabled = false

        lifecycleScope.launch {
            try {
                model?.close()
                
                model = LlamaModel.load(modelPath) {
                    contextSize = 2048
                    threads = 4
                    temperature = 0.7f
                    topP = 0.9f
                    topK = 40
                }
                
                // Save model path for auto-loading
                prefs.edit().putString(KEY_MODEL_PATH, modelPath).apply()
                
                binding.tvStatus.text = "Model loaded: ${File(modelPath).name}"
                binding.btnGenerate.isEnabled = true
                Toast.makeText(this@MainActivity, "Model loaded successfully!", Toast.LENGTH_SHORT).show()
                
            } catch (e: LlamaException.ModelNotFound) {
                binding.tvStatus.text = "Error: Model not found"
                Toast.makeText(this@MainActivity, "Model file not found: ${e.path}", Toast.LENGTH_LONG).show()
            } catch (e: LlamaException.ModelLoadError) {
                binding.tvStatus.text = "Error: ${e.message}"
                Toast.makeText(this@MainActivity, "Failed to load model: ${e.message}", Toast.LENGTH_LONG).show()
            } catch (e: Exception) {
                binding.tvStatus.text = "Error: ${e.message}"
                Toast.makeText(this@MainActivity, "Error: ${e.message}", Toast.LENGTH_LONG).show()
            } finally {
                binding.btnLoadModel.isEnabled = true
            }
        }
    }

    private fun generateResponse(prompt: String) {
        val currentModel = model
        if (currentModel == null) {
            Toast.makeText(this, "Please load a model first", Toast.LENGTH_SHORT).show()
            return
        }

        binding.tvResponse.text = ""
        binding.btnGenerate.isEnabled = false
        binding.btnCancel.isEnabled = true
        binding.tvStatus.text = "Generating..."

        generationJob = lifecycleScope.launch {
            try {
                currentModel.generateStream(prompt)
                    .catch { e ->
                        withContext(Dispatchers.Main) {
                            binding.tvStatus.text = "Error: ${e.message}"
                        }
                    }
                    .collect { token ->
                        binding.tvResponse.append(token)
                        // Auto-scroll
                        binding.scrollView.post {
                            binding.scrollView.fullScroll(android.view.View.FOCUS_DOWN)
                        }
                    }
                
                binding.tvStatus.text = "Generation complete"
            } catch (e: LlamaException.GenerationCancelled) {
                binding.tvStatus.text = "Generation cancelled"
            } catch (e: Exception) {
                binding.tvStatus.text = "Error: ${e.message}"
            } finally {
                binding.btnGenerate.isEnabled = true
                binding.btnCancel.isEnabled = false
            }
        }
    }

    private fun cancelGeneration() {
        model?.cancelGeneration()
        generationJob?.cancel()
        binding.tvStatus.text = "Cancelled"
        binding.btnGenerate.isEnabled = true
        binding.btnCancel.isEnabled = false
    }

    override fun onDestroy() {
        super.onDestroy()
        model?.close()
    }
}
