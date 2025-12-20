package com.llamakotlin.sample

import android.os.Bundle
import android.os.Environment
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.llamakotlin.android.LlamaConfig
import com.llamakotlin.android.LlamaModel
import com.llamakotlin.android.exception.LlamaException
import com.llamakotlin.sample.databinding.ActivityMainBinding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private var model: LlamaModel? = null
    private var generationJob: Job? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setupUI()
        showVersion()
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

    private val pickFileLauncher = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        uri?.let {
            // Copy to local storage or use content resolver
            val path = getModelPath(uri.path ?: "")
            if (path.isNotEmpty()) {
                loadModel(path)
            } else {
                Toast.makeText(this, "Invalid model file", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun pickModelFile() {
        // For simplicity, look for model in Downloads folder
        val downloadsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
        val ggufFiles = downloadsDir.listFiles { file -> file.extension == "gguf" }
        
        if (ggufFiles.isNullOrEmpty()) {
            Toast.makeText(this, "No .gguf files in Downloads. Please download a model first.", Toast.LENGTH_LONG).show()
            binding.tvStatus.text = "No model found. Download a .gguf model to Downloads folder."
            return
        }

        // Use the first found model
        val modelFile = ggufFiles.first()
        loadModel(modelFile.absolutePath)
    }

    private fun getModelPath(uriPath: String): String {
        // Simple path extraction - in production use ContentResolver
        return uriPath
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
