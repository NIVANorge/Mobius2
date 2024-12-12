
import pandas as pd
import os

def xlsx_input_from_dataframe(file, df, sheet_name, indexes = {}, flags = None) :
	'''
	Write a dataframe to an xlsx sheet, potentially with Mobius2 indexing and series flags. If the sheet already exists, it is cleared and overwritten.
	
	Arguments:
		file - Either a string (file path) or a pandas.ExcelWriter . If it is a pandas.ExcelWriter it must use an 'openpyxl' engine.
		df   - A pandas.DataFrame . It must be formatted in a way that makes sense as a Mobius2 input, for instance with correct column names, and with datetime indexing.
		sheet_name - string, name of the sheet to write to.
		indexes - A dict  {index_set : index_names}, where for each entry the index_set is the (string) name of an index_set in the relevant model, and index_names is either:
			1. A single index name, which will be the index for that index set for all series in the dataframe.
			2. A list of index names, one for each column in the dataframe. If too few values are passed, the later columns will not be indexed over this index_set.
		flags - One of the following
			1. None
			2. A single string which will be the flags value for all series in the dataframe
			3. A list of flags strings, one for each column in the dataframe. If too few values are passed, the later columns will have blank flags.
	'''
	
	if not isinstance(df.index, pd.DatetimeIndex) :
		raise ValueError('A pandas.DataFrame that is indexed by datetimes is expected.')
	
	if isinstance(file, str) :
		opened_here = True
		mode = 'w'
		if os.path.isfile(file) :
			mode = 'a'
		writer = pd.ExcelWriter(file, engine='openpyxl', if_sheet_exists='replace', mode=mode)
	else :
		opened_here = False
		writer = file
		
		if (not isinstance(file, pd.ExcelWriter)) or (file.engine != 'openpyxl') :
			raise ValueError('The file argument must be either a path (string) or a pandas.ExcelWriter with an openpyxl engine')
	
	book = writer.book
	
	# If some dates are before 1900-03-01 we have to use string format for the dates, otherwise the excel formatting is broken
	df2 = df
	if any(df.index < '1900-03-01') :
		df2 = df.copy()
		df2.index = df2.index.strftime('%Y-%m-%d')  # TODO: Do we need to handle h:m:s also?
	
	
	df2.to_excel(writer, sheet_name=sheet_name)
	
	# Find the book we wrote to
	sheet = book[sheet_name]
	
	# Make it so that the date column displays correctly and is not truncated
	sheet.column_dimensions['A'].width = 30
	
	# Write rows describing indexing and potentially flags
	n_sets = len(indexes)
	n_rows = n_sets
	if flags : n_rows+=1
	
	if n_rows > 0 :
		sheet.insert_rows(2, n_rows)
	
	for i, index_set in enumerate(indexes) :
		# The cell containing the name of the index set
		cell0 = sheet.cell(row=i+2, column=1)
		cell0.value = index_set
		# The cells containing the index names
		for j in range(len(df.columns)) :
			idxs = indexes[index_set]
			if isinstance(idxs, list) :
				if j < len(idxs) :
					idx = idxs[j]
				else : break
			else :
				idx = idxs
			cell = sheet.cell(row=i+2, column=j+2)
			cell.value = idx
			
	if flags :
		row = n_rows+1
		for j in range(len(df.columns)) :
			if isinstance(flags, list) :
				if j < len(flags) :
					flg = flags[j]
				else : break
			else :
				flg = flags
			cell = sheet.cell(row=row, column=j+2)
			cell.value = flg
	
	
	if opened_here :
		writer.close()